#include "openfhe.h"

#include <chrono>
#include <complex>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace lbcrypto;

namespace {

uint32_t GetEnvUInt(const char* name, uint32_t defaultValue) {
    if (const char* raw = std::getenv(name)) {
        try {
            return static_cast<uint32_t>(std::stoul(raw));
        }
        catch (...) {
        }
    }
    return defaultValue;
}

double MeanAbsDiff(const std::vector<std::complex<double>>& lhs, const std::vector<std::complex<double>>& rhs) {
    const size_t n = std::min(lhs.size(), rhs.size());
    double total = 0.0;
    for (size_t i = 0; i < n; ++i) {
        total += std::abs(lhs[i] - rhs[i]);
    }
    return (n == 0) ? 0.0 : total / static_cast<double>(n);
}

}  // namespace

int main() {
    const uint32_t slots = GetEnvUInt("OPENFHE_NCLT_SLOTS", 64);
    const uint32_t ringDim = GetEnvUInt("OPENFHE_NCLT_RING_DIM", 1u << 12);
    const uint32_t dcrtBits = GetEnvUInt("OPENFHE_NCLT_DCRT_BITS", 50);
    const uint32_t firstMod = GetEnvUInt("OPENFHE_NCLT_FIRST_MOD", 60);
    const uint32_t iters = GetEnvUInt("OPENFHE_NCLT_ITERS", 3);
    const std::string outCsv = std::getenv("OPENFHE_NCLT_OUTPUT") ? std::getenv("OPENFHE_NCLT_OUTPUT")
                                                                  : "/workspace/results/nc_lineartransform_bench.csv";

    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(ringDim);
    parameters.SetScalingTechnique(FLEXIBLEAUTO);
    parameters.SetScalingModSize(dcrtBits);
    parameters.SetFirstModSize(firstMod);
    parameters.SetMultiplicativeDepth(FHECKKSRNS::GetBootstrapDepth({3, 3}, UNIFORM_TERNARY) + 4);

    CryptoContext<DCRTPoly> cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    cc->Enable(ADVANCEDSHE);
    cc->Enable(FHE);
    cc->EvalBootstrapSetup({3, 3}, {0, 0}, slots);

    auto kp = cc->KeyGen();

    const uint32_t g = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(slots))));
    const uint32_t h = static_cast<uint32_t>(std::ceil(static_cast<double>(slots) / static_cast<double>(g)));

    std::set<int32_t> indexSet;
    for (uint32_t i = 1; i < slots; ++i) {
        indexSet.insert(static_cast<int32_t>(i));
        indexSet.insert(-static_cast<int32_t>(i));
    }
    std::vector<uint32_t> outerRotations;
    for (uint32_t j = 1; j < h; ++j) {
        const uint32_t rot = g * j;
        if (rot < slots) {
            outerRotations.push_back(rot);
        }
    }
    cc->EvalRotateKeyGen(kp.secretKey, std::vector<int32_t>(indexSet.begin(), indexSet.end()));

    std::vector<std::vector<std::complex<double>>> matrix(slots, std::vector<std::complex<double>>(slots));
    for (uint32_t row = 0; row < slots; ++row) {
        matrix[row][row] = std::complex<double>(0.25, 0.0);
        for (uint32_t rot : outerRotations) {
            matrix[row][(row + rot) % slots] += std::complex<double>(0.05, 0.0);
        }
        for (uint32_t rot = 1; rot < std::min<uint32_t>(g, slots); ++rot) {
            matrix[row][(row + rot) % slots] += std::complex<double>(0.02, 0.0);
        }
    }

    auto precomp = cc->EvalLinearTransformPrecompute(matrix, 1.0, 0);

    std::mt19937 rng(7);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::vector<std::complex<double>> values(slots);
    for (auto& value : values) {
        value = {dist(rng), dist(rng)};
    }

    auto pt = cc->MakeCKKSPackedPlaintext(values);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    std::ofstream out(outCsv);
    out << "mode,latency_ms,mae_vs_standard,slots,ring_dim\n";
    out << std::fixed << std::setprecision(9);

    std::vector<std::string> modes = {"standard", "generator", "delayed"};
    std::map<std::string, Ciphertext<DCRTPoly>> outputs;
    std::map<std::string, double> latencies;

    for (const auto& mode : modes) {
        setenv("OPENFHE_NC_LT_MODE", mode.c_str(), 1);
        double totalMs = 0.0;
        Ciphertext<DCRTPoly> outCt;
        for (uint32_t iter = 0; iter < iters; ++iter) {
            auto t0 = std::chrono::high_resolution_clock::now();
            auto tmp = cc->EvalLinearTransform(precomp, ct);
            auto t1 = std::chrono::high_resolution_clock::now();
            totalMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
            if (iter == 0) {
                outCt = tmp;
            }
        }
        outputs[mode] = outCt;
        latencies[mode] = totalMs / static_cast<double>(iters);
    }

    unsetenv("OPENFHE_NC_LT_MODE");

    Plaintext standardPt;
    cc->Decrypt(kp.secretKey, outputs["standard"], &standardPt);
    standardPt->SetLength(slots);

    for (const auto& mode : modes) {
        Plaintext currentPt;
        cc->Decrypt(kp.secretKey, outputs[mode], &currentPt);
        currentPt->SetLength(slots);
        const double mae = MeanAbsDiff(standardPt->GetCKKSPackedValue(), currentPt->GetCKKSPackedValue());
        out << mode << ',' << latencies[mode] << ',' << mae << ',' << slots << ',' << ringDim << '\n';
    }

    std::cout << "wrote " << outCsv << std::endl;
    return 0;
}
