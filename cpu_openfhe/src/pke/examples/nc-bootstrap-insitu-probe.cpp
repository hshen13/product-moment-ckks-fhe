#include "openfhe.h"

#include <chrono>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace lbcrypto;

namespace {

uint32_t GetEnvUInt(const char* name, uint32_t fallback) {
    if (const char* raw = std::getenv(name)) {
        try {
            return static_cast<uint32_t>(std::stoul(raw));
        }
        catch (...) {
        }
    }
    return fallback;
}

std::string GetEnvString(const char* name, const std::string& fallback) {
    if (const char* raw = std::getenv(name)) {
        return std::string(raw);
    }
    return fallback;
}

double MeanAbsDiff(const std::vector<std::complex<double>>& lhs, const std::vector<std::complex<double>>& rhs) {
    const size_t n = std::min(lhs.size(), rhs.size());
    double total  = 0.0;
    for (size_t i = 0; i < n; ++i) {
        total += std::abs(lhs[i] - rhs[i]);
    }
    return (n == 0) ? 0.0 : total / static_cast<double>(n);
}

bool IsFinite(const std::vector<std::complex<double>>& values) {
    for (const auto& value : values) {
        if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
            return false;
        }
    }
    return true;
}

template <typename Fn>
Ciphertext<DCRTPoly> TimeMode(const std::string& mode, uint32_t iters, double& latencyMs, Fn&& fn) {
    setenv("OPENFHE_NC_FFT_MODE", mode.c_str(), 1);
    Ciphertext<DCRTPoly> out;
    double total = 0.0;
    for (uint32_t i = 0; i < iters; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto tmp = fn();
        auto t1 = std::chrono::high_resolution_clock::now();
        total += std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (i == 0) {
            out = tmp;
        }
    }
    unsetenv("OPENFHE_NC_FFT_MODE");
    latencyMs = total / static_cast<double>(iters);
    return out;
}

}  // namespace

int main() {
    const uint32_t ringDim = GetEnvUInt("OPENFHE_NC_PROBE_RING_DIM", 1u << 12);
    const uint32_t dcrtBits = GetEnvUInt("OPENFHE_NC_PROBE_DCRT_BITS", 59);
    const uint32_t firstMod = GetEnvUInt("OPENFHE_NC_PROBE_FIRST_MOD", 60);
    const uint32_t levelsAfter = GetEnvUInt("OPENFHE_NC_PROBE_LEVELS_AFTER", 10);
    const uint32_t iters = GetEnvUInt("OPENFHE_NC_PROBE_ITERS", 1);
    const std::string outCsv = GetEnvString("OPENFHE_NC_PROBE_OUTPUT",
                                            "/workspace/results/nc_bootstrap_insitu_probe.csv");

    SecretKeyDist secretKeyDist = UNIFORM_TERNARY;
    std::vector<uint32_t> levelBudget = {4, 4};
    uint32_t depth = levelsAfter + FHECKKSRNS::GetBootstrapDepth(levelBudget, secretKeyDist);

    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetSecretKeyDist(secretKeyDist);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(ringDim);
    parameters.SetScalingTechnique(FLEXIBLEAUTO);
    parameters.SetScalingModSize(dcrtBits);
    parameters.SetFirstModSize(firstMod);
    parameters.SetMultiplicativeDepth(depth);

    CryptoContext<DCRTPoly> cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    cc->Enable(ADVANCEDSHE);
    cc->Enable(FHE);

    const uint32_t slots = cc->GetRingDimension() / 2;
    cc->EvalBootstrapSetup(levelBudget, {0, 0}, slots);
    auto kp = cc->KeyGen();
    cc->EvalMultKeyGen(kp.secretKey);
    cc->EvalBootstrapKeyGen(kp.secretKey, slots);

    std::mt19937 rng(7);
    std::uniform_real_distribution<double> dist(-0.25, 0.25);
    std::vector<std::complex<double>> values(slots);
    for (auto& value : values) {
        value = {dist(rng), dist(rng)};
    }
    auto pt = cc->MakeCKKSPackedPlaintext(values, 1, depth - 1, nullptr, slots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    std::ofstream out(outCsv);
    out << "stage,mode,status,finite,latency_ms,mae_vs_standard,error,slots,ring_dim\n";
    out << std::fixed << std::setprecision(9);

    const std::vector<std::string> stages = {"coeffstoslots", "slotstocoeffs"};
    for (const auto& stage : stages) {
        double stdMs = 0.0;
        Ciphertext<DCRTPoly> stdCt;
        try {
            if (stage == "coeffstoslots") {
                stdCt = TimeMode("standard", iters, stdMs, [&]() { return cc->EvalBootstrapProbeCoeffsToSlots(ct); });
            }
            else {
                stdCt = TimeMode("standard", iters, stdMs, [&]() { return cc->EvalBootstrapProbeSlotsToCoeffs(ct); });
            }
        }
        catch (const std::exception& e) {
            out << stage << ",standard,error,0,0.0,0.0,\"" << e.what() << "\"," << slots << ',' << ringDim << '\n';
            continue;
        }

        Plaintext stdPt;
        cc->Decrypt(kp.secretKey, stdCt, &stdPt);
        stdPt->SetLength(slots);
        const auto stdVals = stdPt->GetCKKSPackedValue();
        out << stage << ",standard,ok," << (IsFinite(stdVals) ? 1 : 0) << ',' << stdMs << ",0.000000000,\"\","
            << slots << ',' << ringDim << '\n';

        for (const auto& mode : {"generator", "delayed"}) {
            try {
                double ms = 0.0;
                Ciphertext<DCRTPoly> curCt;
                if (stage == "coeffstoslots") {
                    curCt = TimeMode(mode, iters, ms, [&]() { return cc->EvalBootstrapProbeCoeffsToSlots(ct); });
                }
                else {
                    curCt = TimeMode(mode, iters, ms, [&]() { return cc->EvalBootstrapProbeSlotsToCoeffs(ct); });
                }

                Plaintext curPt;
                cc->Decrypt(kp.secretKey, curCt, &curPt);
                curPt->SetLength(slots);
                const auto curVals = curPt->GetCKKSPackedValue();
                out << stage << ',' << mode << ",ok," << (IsFinite(curVals) ? 1 : 0) << ',' << ms << ','
                    << MeanAbsDiff(stdVals, curVals) << ",\"\"," << slots << ',' << ringDim << '\n';
            }
            catch (const std::exception& e) {
                out << stage << ',' << mode << ",error,0,0.0,0.0,\"" << e.what() << "\"," << slots << ',' << ringDim
                    << '\n';
            }
        }
    }

    std::cout << "wrote " << outCsv << std::endl;
    return 0;
}
