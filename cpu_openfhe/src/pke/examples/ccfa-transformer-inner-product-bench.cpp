#include "openfhe.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
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

void ClearCcfaEnv() {
    setenv("OPENFHE_CCFA_MODE", "none", 1);
    unsetenv("OPENFHE_CCFA_DIST");
    unsetenv("OPENFHE_CCFA_COUPLING");
    unsetenv("OPENFHE_CCFA_KEEP_PROB");
    unsetenv("OPENFHE_CCFA_MIN_SCALE");
    unsetenv("OPENFHE_CCFA_SEED");
    unsetenv("OPENFHE_CCFA_MIN_M");
    unsetenv("OPENFHE_CCFA_MAX_M");
    unsetenv("OPENFHE_CCFA_SAFE_BOOTSTRAP");
    unsetenv("OPENFHE_CCFA_SAFE_MAX_M");
    unsetenv("OPENFHE_CCFA_SAFE_DISABLE_CU");
    unsetenv("OPENFHE_CCFA_PROTECT_HEAD");
    unsetenv("OPENFHE_CCFA_PROTECT_TAIL");
    unsetenv("OPENFHE_CCFA_ELIGIBLE_REL_ABS");
}

void SetCcfaEnv(const std::string& mode, double keepProb, uint32_t seed) {
    if (mode == "deterministic") {
        ClearCcfaEnv();
        return;
    }
    setenv("OPENFHE_CCFA_MODE", mode == "product_coupled" ? "product" : "independent", 1);
    setenv("OPENFHE_CCFA_DIST", "bernoulli", 1);
    setenv("OPENFHE_CCFA_COUPLING", mode == "product_coupled" ? "product_coupled" : "independent", 1);
    setenv("OPENFHE_CCFA_KEEP_PROB", std::to_string(keepProb).c_str(), 1);
    setenv("OPENFHE_CCFA_MIN_SCALE", std::to_string(ReadEnvDouble("OPENFHE_CCFA_E10_MIN_SCALE", 0.0)).c_str(), 1);
    setenv("OPENFHE_CCFA_SEED", std::to_string(seed).c_str(), 1);
    setenv("OPENFHE_CCFA_MIN_M", "2", 1);
    setenv("OPENFHE_CCFA_MAX_M", "16", 1);
}

std::vector<int32_t> SumRotations(uint32_t slots) {
    std::vector<int32_t> rotations;
    for (uint32_t step = 1; step < slots; step <<= 1) {
        rotations.push_back(static_cast<int32_t>(step));
    }
    return rotations;
}

std::vector<double> RandomVector(uint32_t dim, uint32_t slots, uint32_t seed, double scale) {
    std::mt19937 gen(seed);
    std::normal_distribution<double> dist(0.0, scale);
    std::vector<double> out(slots, 0.0);
    for (uint32_t i = 0; i < dim; ++i) {
        out[i] = std::max(-0.95, std::min(0.95, dist(gen)));
    }
    return out;
}

std::vector<double> AddNoise(const std::vector<double>& base, uint32_t dim, uint32_t seed, double scale) {
    std::mt19937 gen(seed);
    std::normal_distribution<double> dist(0.0, scale);
    std::vector<double> out = base;
    for (uint32_t i = 0; i < dim; ++i) {
        out[i] = std::max(-0.95, std::min(0.95, base[i] + dist(gen)));
    }
    return out;
}

std::vector<double> DecryptReal(CryptoContext<DCRTPoly> cc, const PrivateKey<DCRTPoly>& sk,
                                ConstCiphertext<DCRTPoly>& ct, uint32_t length) {
    Plaintext dec;
    cc->Decrypt(sk, ct, &dec);
    dec->SetLength(length);
    std::vector<double> out;
    out.reserve(length);
    for (const auto& value : dec->GetCKKSPackedValue()) {
        out.push_back(value.real());
    }
    return out;
}

Ciphertext<DCRTPoly> RotateSum(CryptoContext<DCRTPoly> cc, Ciphertext<DCRTPoly> ct, uint32_t slots) {
    for (uint32_t step = 1; step < slots; step <<= 1) {
        ct = cc->EvalAdd(ct, cc->EvalRotate(ct, static_cast<int32_t>(step)));
    }
    return ct;
}

double Correlation(const std::vector<double>& a, const std::vector<double>& b, uint32_t dim) {
    double ma = 0.0;
    double mb = 0.0;
    for (uint32_t i = 0; i < dim; ++i) {
        ma += a[i];
        mb += b[i];
    }
    ma /= static_cast<double>(dim);
    mb /= static_cast<double>(dim);
    double num = 0.0;
    double va = 0.0;
    double vb = 0.0;
    for (uint32_t i = 0; i < dim; ++i) {
        const double da = a[i] - ma;
        const double db = b[i] - mb;
        num += da * db;
        va += da * da;
        vb += db * db;
    }
    const double den = std::sqrt(va * vb);
    return den > 0.0 ? num / den : 0.0;
}

struct Runtime {
    CryptoContext<DCRTPoly> cc;
    KeyPair<DCRTPoly> kp;
    uint32_t slots = 0;
    uint32_t depth = 0;
    Plaintext mask;
};

Runtime Setup(uint32_t ringDim, uint32_t slots, uint32_t depth, uint32_t dim) {
    Runtime rt;
    rt.slots = slots;
    rt.depth = depth;

    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(ringDim);
    parameters.SetBatchSize(slots);
    parameters.SetScalingTechnique(FLEXIBLEAUTO);
    parameters.SetScalingModSize(ReadEnvUInt("OPENFHE_CCFA_E10_DCRT_BITS", 50));
    parameters.SetFirstModSize(ReadEnvUInt("OPENFHE_CCFA_E10_FIRST_MOD", 60));
    parameters.SetMultiplicativeDepth(depth);

    rt.cc = GenCryptoContext(parameters);
    rt.cc->Enable(PKE);
    rt.cc->Enable(KEYSWITCH);
    rt.cc->Enable(LEVELEDSHE);
    rt.cc->Enable(ADVANCEDSHE);
    rt.kp = rt.cc->KeyGen();
    rt.cc->EvalMultKeyGen(rt.kp.secretKey);
    rt.cc->EvalRotateKeyGen(rt.kp.secretKey, SumRotations(slots));

    std::vector<double> mask(slots, 0.0);
    for (uint32_t i = 0; i < dim; ++i) {
        mask[i] = 1.0;
    }
    rt.mask = rt.cc->MakeCKKSPackedPlaintext(mask, 1, 0, nullptr, slots);
    return rt;
}

struct Trial {
    std::string mode;
    uint32_t pair;
    uint32_t seed;
    bool success = false;
    double latencyMs = 0.0;
    double detScore = 0.0;
    double score = 0.0;
    double absError = 0.0;
    double relError = 0.0;
    double qErrorCorr = 0.0;
    std::string error;
};

Trial RunTrial(const Runtime& rt, const std::vector<double>& x, const std::vector<double>& y, uint32_t dim,
               uint32_t degree, double detScore, const std::vector<double>& detX, const std::vector<double>& detY,
               const std::string& mode, double keepProb, uint32_t pair, uint32_t seed) {
    Trial row;
    row.mode = mode;
    row.pair = pair;
    row.seed = seed;
    row.detScore = detScore;
    try {
        SetCcfaEnv(mode, keepProb, seed);
        auto ptX = rt.cc->MakeCKKSPackedPlaintext(x, 1, 0, nullptr, rt.slots);
        auto ptY = rt.cc->MakeCKKSPackedPlaintext(y, 1, 0, nullptr, rt.slots);
        auto ctX = rt.cc->Encrypt(rt.kp.publicKey, ptX);
        auto ctY = rt.cc->Encrypt(rt.kp.publicKey, ptY);

        const auto start = std::chrono::steady_clock::now();
        auto actX = rt.cc->EvalChebyshevFunction([](double v) { return v < 0.0 ? 0.0 : v; }, ctX, -1.0, 1.0, degree);
        auto actY = rt.cc->EvalChebyshevFunction([](double v) { return v < 0.0 ? 0.0 : v; }, ctY, -1.0, 1.0, degree);
        auto prod = rt.cc->EvalMult(actX, actY);
        prod = rt.cc->EvalMult(prod, rt.mask);
        auto sum = RotateSum(rt.cc, prod, rt.slots);
        const auto stop = std::chrono::steady_clock::now();

        auto scoreVec = DecryptReal(rt.cc, rt.kp.secretKey, sum, 1);
        auto randX = DecryptReal(rt.cc, rt.kp.secretKey, actX, rt.slots);
        auto randY = DecryptReal(rt.cc, rt.kp.secretKey, actY, rt.slots);
        std::vector<double> errX(dim);
        std::vector<double> errY(dim);
        for (uint32_t i = 0; i < dim; ++i) {
            errX[i] = randX[i] - detX[i];
            errY[i] = randY[i] - detY[i];
        }

        row.success = true;
        row.latencyMs = std::chrono::duration<double, std::milli>(stop - start).count();
        row.score = scoreVec.empty() ? 0.0 : scoreVec[0];
        row.absError = std::abs(row.score - detScore);
        row.relError = row.absError / std::max(std::abs(detScore), 1e-12);
        row.qErrorCorr = Correlation(errX, errY, dim);
    }
    catch (const std::exception& e) {
        row.error = e.what();
    }
    ClearCcfaEnv();
    return row;
}

}  // namespace

int main() {
    const uint32_t dim = ReadEnvUInt("OPENFHE_CCFA_E10_DIM", 768);
    const uint32_t ringDim = ReadEnvUInt("OPENFHE_CCFA_E10_RING_DIM", 1u << 12);
    const uint32_t slots = ReadEnvUInt("OPENFHE_CCFA_E10_SLOTS", 1024);
    const uint32_t depth = ReadEnvUInt("OPENFHE_CCFA_E10_DEPTH", 12);
    const uint32_t degree = ReadEnvUInt("OPENFHE_CCFA_E10_DEGREE", 59);
    const uint32_t pairs = ReadEnvUInt("OPENFHE_CCFA_E10_PAIRS", 100);
    const uint32_t firstSeed = ReadEnvUInt("OPENFHE_CCFA_E10_FIRST_SEED", 1);
    const double keepProb = ReadEnvDouble("OPENFHE_CCFA_E10_KEEP_PROB", 0.60);
    const double inputScale = ReadEnvDouble("OPENFHE_CCFA_E10_INPUT_SCALE", 0.35);
    const double pairNoise = ReadEnvDouble("OPENFHE_CCFA_E10_PAIR_NOISE", 0.05);
    const bool correlatedPairs = ReadEnvUInt("OPENFHE_CCFA_E10_CORRELATED_PAIRS", 1) != 0;
    const bool fixedInput = ReadEnvUInt("OPENFHE_CCFA_E10_FIXED_INPUT", 0) != 0;
    const uint32_t fixedInputSeed = ReadEnvUInt("OPENFHE_CCFA_E10_FIXED_INPUT_SEED", firstSeed);
    const std::string outCsv =
        ReadEnvString("OPENFHE_CCFA_E10_OUTPUT", "/workspace/results/ccs2026_product/e10_inner_product.csv");

    if (dim > slots) {
        OPENFHE_THROW("OPENFHE_CCFA_E10_DIM must be <= OPENFHE_CCFA_E10_SLOTS");
    }

    std::filesystem::create_directories(std::filesystem::path(outCsv).parent_path());
    Runtime rt = Setup(ringDim, slots, depth, dim);
    std::ofstream out(outCsv);
    out << "mode,pair,seed,success,latency_ms,det_score,score,abs_error,rel_error,error_corr,error,dim,slots,degree,keep_prob\n";
    out << std::scientific << std::setprecision(12);

    for (uint32_t pair = 0; pair < pairs; ++pair) {
        const uint32_t seed = firstSeed + pair;
        const uint32_t inputSeed = fixedInput ? fixedInputSeed : seed;
        const auto x = RandomVector(dim, slots, 17 * inputSeed + 3, inputScale);
        const auto y =
            correlatedPairs ? AddNoise(x, dim, 31 * inputSeed + 7, pairNoise) :
                              RandomVector(dim, slots, 31 * inputSeed + 7, inputScale);

        auto det = RunTrial(rt, x, y, dim, degree, 0.0, std::vector<double>(slots, 0.0),
                            std::vector<double>(slots, 0.0), "deterministic", keepProb, pair, seed);
        std::vector<double> detX(slots, 0.0);
        std::vector<double> detY(slots, 0.0);
        double detScore = det.score;
        det.detScore = detScore;
        det.absError = 0.0;
        det.relError = 0.0;
        det.qErrorCorr = 0.0;
        if (det.success) {
            ClearCcfaEnv();
            auto ptX = rt.cc->MakeCKKSPackedPlaintext(x, 1, 0, nullptr, rt.slots);
            auto ptY = rt.cc->MakeCKKSPackedPlaintext(y, 1, 0, nullptr, rt.slots);
            auto ctX = rt.cc->Encrypt(rt.kp.publicKey, ptX);
            auto ctY = rt.cc->Encrypt(rt.kp.publicKey, ptY);
            auto actX =
                rt.cc->EvalChebyshevFunction([](double v) { return v < 0.0 ? 0.0 : v; }, ctX, -1.0, 1.0, degree);
            auto actY =
                rt.cc->EvalChebyshevFunction([](double v) { return v < 0.0 ? 0.0 : v; }, ctY, -1.0, 1.0, degree);
            detX = DecryptReal(rt.cc, rt.kp.secretKey, actX, rt.slots);
            detY = DecryptReal(rt.cc, rt.kp.secretKey, actY, rt.slots);
        }

        for (const auto& row :
             std::vector<Trial>{det,
                                RunTrial(rt, x, y, dim, degree, detScore, detX, detY, "independent", keepProb, pair,
                                         seed),
                                RunTrial(rt, x, y, dim, degree, detScore, detX, detY, "product_coupled", keepProb,
                                         pair, seed)}) {
            out << row.mode << ',' << row.pair << ',' << row.seed << ',' << (row.success ? 1 : 0) << ','
                << row.latencyMs << ',' << row.detScore << ',' << row.score << ',' << row.absError << ','
                << row.relError << ',' << row.qErrorCorr << ",\"" << row.error << "\"," << dim << ',' << slots
                << ',' << degree << ',' << keepProb << '\n';
        }
    }

    std::cout << "Wrote transformer-scale inner product results to " << outCsv << std::endl;
    return 0;
}
