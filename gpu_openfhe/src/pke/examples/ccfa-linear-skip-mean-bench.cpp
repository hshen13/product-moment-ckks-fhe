#include "openfhe.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using namespace lbcrypto;

namespace {

uint32_t ReadEnvUInt(const char* name, uint32_t fallback) {
    const char* value = std::getenv(name);
    return value ? static_cast<uint32_t>(std::strtoul(value, nullptr, 10)) : fallback;
}

double ReadEnvDouble(const char* name, double fallback) {
    const char* value = std::getenv(name);
    return value ? std::stod(value) : fallback;
}

std::string ReadEnvString(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : fallback;
}

struct Runtime {
    CryptoContext<DCRTPoly> cc;
    KeyPair<DCRTPoly> kp;
    uint32_t slots = 0;
};

Runtime Setup(uint32_t ringDim, uint32_t slots) {
    Runtime rt;
    rt.slots = slots;

    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(ringDim);
    parameters.SetBatchSize(slots);
    parameters.SetScalingTechnique(FLEXIBLEAUTO);
    parameters.SetScalingModSize(ReadEnvUInt("OPENFHE_CCFA_LINEAR_DCRT_BITS", 50));
    parameters.SetFirstModSize(ReadEnvUInt("OPENFHE_CCFA_LINEAR_FIRST_MOD", 60));
    parameters.SetMultiplicativeDepth(2);

    rt.cc = GenCryptoContext(parameters);
    rt.cc->Enable(PKE);
    rt.cc->Enable(KEYSWITCH);
    rt.cc->Enable(LEVELEDSHE);
    rt.kp = rt.cc->KeyGen();
    rt.cc->EvalMultKeyGen(rt.kp.secretKey);
    return rt;
}

std::vector<double> DecryptReal(const Runtime& rt, ConstCiphertext<DCRTPoly>& ct, size_t length) {
    Plaintext dec;
    rt.cc->Decrypt(rt.kp.secretKey, ct, &dec);
    dec->SetLength(length);
    std::vector<double> out;
    out.reserve(length);
    for (const auto& value : dec->GetCKKSPackedValue()) {
        out.push_back(value.real());
    }
    return out;
}

Ciphertext<DCRTPoly> EvalSparseLinearSum(const Runtime& rt, const std::vector<Ciphertext<DCRTPoly>>& cts,
                                         const std::vector<double>& coeffs, uint32_t& activeTerms) {
    activeTerms = 0;
    Ciphertext<DCRTPoly> acc;
    for (size_t i = 0; i < coeffs.size(); ++i) {
        if (std::abs(coeffs[i]) <= 0.0) {
            continue;
        }
        auto term = rt.cc->EvalMult(cts[i], coeffs[i]);
        if (!acc) {
            acc = term;
        }
        else {
            rt.cc->EvalAddInPlace(acc, term);
        }
        ++activeTerms;
    }
    if (!acc) {
        acc = rt.cc->EvalMult(cts.front(), 0.0);
    }
    return acc;
}

double MeanAbsError(const std::vector<double>& got, const std::vector<double>& ref) {
    double total = 0.0;
    for (size_t i = 0; i < got.size(); ++i) {
        total += std::abs(got[i] - ref[i]);
    }
    return total / static_cast<double>(got.size());
}

double MeanSignedError(const std::vector<double>& got, const std::vector<double>& ref) {
    double total = 0.0;
    for (size_t i = 0; i < got.size(); ++i) {
        total += got[i] - ref[i];
    }
    return total / static_cast<double>(got.size());
}

}  // namespace

int main() {
    const uint32_t ringDim = ReadEnvUInt("OPENFHE_CCFA_LINEAR_RING_DIM", 1u << 12);
    const uint32_t slots = ReadEnvUInt("OPENFHE_CCFA_LINEAR_SLOTS", 1024);
    const uint32_t terms = ReadEnvUInt("OPENFHE_CCFA_LINEAR_TERMS", 256);
    const uint32_t trials = ReadEnvUInt("OPENFHE_CCFA_LINEAR_TRIALS", 200);
    const uint32_t firstSeed = ReadEnvUInt("OPENFHE_CCFA_LINEAR_FIRST_SEED", 1);
    const double keepProb = ReadEnvDouble("OPENFHE_CCFA_LINEAR_KEEP_PROB", 0.5);
    const std::string outCsv =
        ReadEnvString("OPENFHE_CCFA_LINEAR_OUTPUT", "/workspace/results/ccs2026_skip_mean/linear_skip_mean.csv");

    if (keepProb <= 0.0 || keepProb > 1.0) {
        OPENFHE_THROW("OPENFHE_CCFA_LINEAR_KEEP_PROB must be in (0,1]");
    }

    std::filesystem::create_directories(std::filesystem::path(outCsv).parent_path());
    Runtime rt = Setup(ringDim, slots);

    std::mt19937 gen(12345);
    std::uniform_real_distribution<double> valueDist(0.0, 1.0);
    std::uniform_real_distribution<double> coeffDist(0.1, 1.0);

    std::vector<std::vector<double>> values(terms, std::vector<double>(slots));
    std::vector<Ciphertext<DCRTPoly>> cts;
    cts.reserve(terms);
    for (uint32_t t = 0; t < terms; ++t) {
        for (uint32_t s = 0; s < slots; ++s) {
            values[t][s] = valueDist(gen);
        }
        auto pt = rt.cc->MakeCKKSPackedPlaintext(values[t], 1, 1, nullptr, slots);
        pt->SetLength(slots);
        cts.push_back(rt.cc->Encrypt(rt.kp.publicKey, pt));
    }

    std::vector<double> coeffs(terms);
    for (auto& coeff : coeffs) {
        coeff = coeffDist(gen);
    }

    std::vector<double> exact(slots, 0.0);
    for (uint32_t t = 0; t < terms; ++t) {
        for (uint32_t s = 0; s < slots; ++s) {
            exact[s] += coeffs[t] * values[t][s];
        }
    }

    std::ofstream out(outCsv);
    out << "mode,trial,seed,latency_ms,active_terms,skipped_terms,mae_vs_exact,mean_signed_vs_exact\n";
    out << std::scientific << std::setprecision(12);

    uint32_t deterministicActive = 0;
    const auto detStart = std::chrono::steady_clock::now();
    auto detCt = EvalSparseLinearSum(rt, cts, coeffs, deterministicActive);
    const auto detStop = std::chrono::steady_clock::now();
    auto det = DecryptReal(rt, detCt, slots);
    out << "deterministic,0,0,"
        << std::chrono::duration<double, std::milli>(detStop - detStart).count() << ',' << deterministicActive << ','
        << (terms - deterministicActive) << ',' << MeanAbsError(det, exact) << ',' << MeanSignedError(det, exact)
        << '\n';

    for (uint32_t trial = 0; trial < trials; ++trial) {
        const uint32_t seed = firstSeed + trial;
        std::mt19937 rng(seed);
        std::bernoulli_distribution keep(keepProb);

        std::vector<double> unscaled(terms, 0.0);
        std::vector<double> restored(terms, 0.0);
        for (uint32_t t = 0; t < terms; ++t) {
            if (keep(rng)) {
                unscaled[t] = coeffs[t];
                restored[t] = coeffs[t] / keepProb;
            }
        }

        for (const auto& item : {std::make_pair("unscaled_skip", unscaled),
                                 std::make_pair("mean_restored_skip", restored)}) {
            uint32_t active = 0;
            const auto start = std::chrono::steady_clock::now();
            auto ct = EvalSparseLinearSum(rt, cts, item.second, active);
            const auto stop = std::chrono::steady_clock::now();
            auto got = DecryptReal(rt, ct, slots);
            out << item.first << ',' << trial << ',' << seed << ','
                << std::chrono::duration<double, std::milli>(stop - start).count() << ',' << active << ','
                << (terms - active) << ',' << MeanAbsError(got, exact) << ',' << MeanSignedError(got, exact) << '\n';
        }
    }

    std::cout << "Wrote linear coefficient skip results to " << outCsv << std::endl;
    return 0;
}
