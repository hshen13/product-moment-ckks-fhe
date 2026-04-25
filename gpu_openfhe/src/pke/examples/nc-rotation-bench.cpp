#include "openfhe.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <set>
#include <sstream>
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

std::string GetEnvString(const char* name, const std::string& defaultValue) {
    if (const char* raw = std::getenv(name)) {
        return std::string(raw);
    }
    return defaultValue;
}

std::vector<int32_t> SignedPow2Chain(int32_t rotation) {
    std::vector<int32_t> steps;
    int32_t sign = (rotation < 0) ? -1 : 1;
    uint32_t value = static_cast<uint32_t>(std::abs(rotation));
    uint32_t bit = 0;
    while (value != 0) {
        if ((value & 1U) != 0U) {
            steps.push_back(sign * static_cast<int32_t>(1U << bit));
        }
        value >>= 1U;
        ++bit;
    }
    return steps;
}

double MeanAbsDiff(const std::vector<std::complex<double>>& lhs, const std::vector<std::complex<double>>& rhs) {
    const size_t width = std::min(lhs.size(), rhs.size());
    if (width == 0) {
        return 0.0;
    }

    double total = 0.0;
    for (size_t i = 0; i < width; ++i) {
        total += std::abs(lhs[i] - rhs[i]);
    }
    return total / static_cast<double>(width);
}

std::vector<int32_t> CollectLinearTransformRotations(uint32_t slots) {
    std::set<int32_t> positive;
    const uint32_t g = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(slots))));
    const uint32_t h = static_cast<uint32_t>(std::ceil(static_cast<double>(slots) / static_cast<double>(g)));

    for (uint32_t i = 1; i <= g; ++i) {
        if (i < slots) {
            positive.insert(static_cast<int32_t>(i));
        }
    }
    for (uint32_t i = 2; i < h; ++i) {
        const uint32_t value = i * g;
        if (value < slots) {
            positive.insert(static_cast<int32_t>(value));
        }
    }

    if (positive.empty()) {
        positive = {1, 3, 7, 15, 31};
    }

    return std::vector<int32_t>(positive.begin(), positive.end());
}

}  // namespace

int main() {
    const uint32_t ringDim   = GetEnvUInt("OPENFHE_NC_RING_DIM", 1u << 12);
    const uint32_t slots     = GetEnvUInt("OPENFHE_NC_SLOTS", 64);
    const uint32_t dcrtBits  = GetEnvUInt("OPENFHE_NC_DCRT_BITS", 50);
    const uint32_t firstMod  = GetEnvUInt("OPENFHE_NC_FIRST_MOD", 60);
    const uint32_t iters     = GetEnvUInt("OPENFHE_NC_ITERS", 5);
    const uint32_t seed      = GetEnvUInt("OPENFHE_NC_SEED", 1337);
    const std::string outCsv = GetEnvString("OPENFHE_NC_OUTPUT", "/workspace/results/nc_rotation_bench.csv");

    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(ringDim);
    parameters.SetScalingTechnique(FLEXIBLEAUTO);
    parameters.SetScalingModSize(dcrtBits);
    parameters.SetFirstModSize(firstMod);

    const std::vector<uint32_t> levelBudget = {3, 3};
    const uint32_t depth = FHECKKSRNS::GetBootstrapDepth(levelBudget, UNIFORM_TERNARY) + 3;
    parameters.SetMultiplicativeDepth(depth);

    CryptoContext<DCRTPoly> cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    cc->Enable(ADVANCEDSHE);
    cc->Enable(FHE);

    cc->EvalBootstrapSetup(levelBudget, {0, 0}, slots);

    auto kp = cc->KeyGen();
    cc->EvalMultKeyGen(kp.secretKey);

    std::vector<int32_t> benchmarkRotations = CollectLinearTransformRotations(slots);
    const int32_t maxRotation = benchmarkRotations.empty() ? 1 : benchmarkRotations.back();

    std::set<int32_t> keySet(benchmarkRotations.begin(), benchmarkRotations.end());
    for (int32_t step = 1; step <= maxRotation; step <<= 1) {
        keySet.insert(step);
        keySet.insert(-step);
    }

    cc->EvalRotateKeyGen(kp.secretKey, std::vector<int32_t>(keySet.begin(), keySet.end()));

    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::vector<std::complex<double>> values(slots);
    for (auto& value : values) {
        value = std::complex<double>(dist(rng), dist(rng));
    }

    Plaintext ptxt = cc->MakeCKKSPackedPlaintext(values);
    auto ct = cc->Encrypt(kp.publicKey, ptxt);

    std::ofstream out(outCsv);
    out << "rotation,chain_steps,popcount,direct_ms,chain_ms,delayed_ms,ratio_chain_over_direct,ratio_delayed_over_direct,mae_chain_vs_direct,mae_delayed_vs_direct,slots,ring_dim\n";
    out << std::fixed << std::setprecision(9);

    for (int32_t rotation : benchmarkRotations) {
        auto chain = SignedPow2Chain(rotation);
        const size_t chainSteps = chain.size();
        double directMs = 0.0;
        double chainMs  = 0.0;
        double delayedMs = 0.0;

        Ciphertext<DCRTPoly> directResult;
        Ciphertext<DCRTPoly> chainResult;
        Ciphertext<DCRTPoly> delayedResult;

        for (uint32_t iter = 0; iter < iters; ++iter) {
            auto t0 = std::chrono::high_resolution_clock::now();
            auto tmpDirect = cc->EvalRotate(ct, rotation);
            auto t1 = std::chrono::high_resolution_clock::now();
            directMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
            if (iter == 0) {
                directResult = tmpDirect;
            }

            auto current = ct;
            auto t2 = std::chrono::high_resolution_clock::now();
            for (int32_t step : chain) {
                current = cc->EvalRotate(current, step);
            }
            auto t3 = std::chrono::high_resolution_clock::now();
            chainMs += std::chrono::duration<double, std::milli>(t3 - t2).count();
            if (iter == 0) {
                chainResult = current;
            }

            auto t4 = std::chrono::high_resolution_clock::now();
            auto tmpDelayed = cc->EvalRotateHoistedChain(ct, std::vector<uint32_t>(chain.begin(), chain.end()));
            auto t5 = std::chrono::high_resolution_clock::now();
            delayedMs += std::chrono::duration<double, std::milli>(t5 - t4).count();
            if (iter == 0) {
                delayedResult = tmpDelayed;
            }
        }

        directMs /= static_cast<double>(iters);
        chainMs /= static_cast<double>(iters);
        delayedMs /= static_cast<double>(iters);

        Plaintext directPtxt;
        Plaintext chainPtxt;
        Plaintext delayedPtxt;
        cc->Decrypt(kp.secretKey, directResult, &directPtxt);
        cc->Decrypt(kp.secretKey, chainResult, &chainPtxt);
        cc->Decrypt(kp.secretKey, delayedResult, &delayedPtxt);
        directPtxt->SetLength(slots);
        chainPtxt->SetLength(slots);
        delayedPtxt->SetLength(slots);

        const double maeChain = MeanAbsDiff(directPtxt->GetCKKSPackedValue(), chainPtxt->GetCKKSPackedValue());
        const double maeDelayed = MeanAbsDiff(directPtxt->GetCKKSPackedValue(), delayedPtxt->GetCKKSPackedValue());
        const double ratio = (directMs > 0.0) ? (chainMs / directMs) : 0.0;
        const double delayedRatio = (directMs > 0.0) ? (delayedMs / directMs) : 0.0;

        out << rotation << ',' << chainSteps << ',' << chainSteps << ',' << directMs << ',' << chainMs << ','
            << delayedMs << ',' << ratio << ',' << delayedRatio << ',' << maeChain << ',' << maeDelayed << ','
            << slots << ',' << ringDim << '\n';
    }

    std::cout << "wrote " << outCsv << " with " << benchmarkRotations.size() << " rotation rows" << std::endl;
    return 0;
}
