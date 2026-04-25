#include "openfhe.h"

#include <chrono>
#include <complex>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
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

template <typename Fn>
Ciphertext<DCRTPoly> TimeMode(CryptoContext<DCRTPoly> cc, const std::string& mode, uint32_t iters, double& latencyMs, Fn&& fn) {
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
    latencyMs = total / static_cast<double>(iters);
    unsetenv("OPENFHE_NC_FFT_MODE");
    return out;
}

}  // namespace

int main() {
    const uint32_t slots = GetEnvUInt("OPENFHE_NCFFT_SLOTS", 64);
    const uint32_t ringDim = GetEnvUInt("OPENFHE_NCFFT_RING_DIM", 1u << 12);
    const uint32_t dcrtBits = GetEnvUInt("OPENFHE_NCFFT_DCRT_BITS", 59);
    const uint32_t firstMod = GetEnvUInt("OPENFHE_NCFFT_FIRST_MOD", 60);
    const uint32_t iters = GetEnvUInt("OPENFHE_NCFFT_ITERS", 1);
    const std::string outCsv = std::getenv("OPENFHE_NCFFT_OUTPUT") ? std::getenv("OPENFHE_NCFFT_OUTPUT")
                                                                   : "/workspace/results/nc_bootstrap_fft_bench.csv";

    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(ringDim);
    parameters.SetScalingTechnique(FLEXIBLEAUTO);
    parameters.SetScalingModSize(dcrtBits);
    parameters.SetFirstModSize(firstMod);
    const uint32_t depth = FHECKKSRNS::GetBootstrapDepth({3, 3}, UNIFORM_TERNARY) + 4;
    parameters.SetMultiplicativeDepth(depth);

    CryptoContext<DCRTPoly> cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    cc->Enable(ADVANCEDSHE);
    cc->Enable(FHE);
    cc->EvalBootstrapSetup({3, 3}, {0, 0}, slots);

    auto kp = cc->KeyGen();
    cc->EvalBootstrapKeyGen(kp.secretKey, slots);

    std::mt19937 rng(11);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::vector<std::complex<double>> values(slots);
    for (auto& value : values) {
        value = {dist(rng), dist(rng)};
    }
    auto pt = cc->MakeCKKSPackedPlaintext(values, 1, depth - 1, nullptr, slots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    std::ofstream out(outCsv);
    out << "op,mode,status,latency_ms,mae_vs_standard,error,slots,ring_dim\n";
    out << std::fixed << std::setprecision(9);

    const std::vector<std::string> ops = {"coeffstoslots", "slotstocoeffs"};
    for (const auto& op : ops) {
        double stdMs = 0.0;
        Ciphertext<DCRTPoly> stdCt;
        try {
            if (op == "coeffstoslots") {
                stdCt = TimeMode(cc, "standard", iters, stdMs, [&]() { return cc->EvalBootstrapCoeffsToSlots(ct, slots); });
            }
            else {
                stdCt = TimeMode(cc, "standard", iters, stdMs, [&]() { return cc->EvalBootstrapSlotsToCoeffs(ct, slots); });
            }
        }
        catch (const std::exception& e) {
            out << op << ",standard,error,0.0,0.0,\"" << e.what() << "\"," << slots << ',' << ringDim << '\n';
            continue;
        }

        Plaintext stdPt;
        cc->Decrypt(kp.secretKey, stdCt, &stdPt);
        stdPt->SetLength(slots);

        out << op << ",standard,ok," << stdMs << ",0.000000000,\"\"" << ',' << slots << ',' << ringDim << '\n';

        const std::vector<std::string> modes = {"generator", "delayed"};
        for (const auto& mode : modes) {
            try {
                double ms = 0.0;
                Ciphertext<DCRTPoly> curCt;
                if (op == "coeffstoslots") {
                    curCt = TimeMode(cc, mode, iters, ms, [&]() { return cc->EvalBootstrapCoeffsToSlots(ct, slots); });
                }
                else {
                    curCt = TimeMode(cc, mode, iters, ms, [&]() { return cc->EvalBootstrapSlotsToCoeffs(ct, slots); });
                }

                Plaintext curPt;
                cc->Decrypt(kp.secretKey, curCt, &curPt);
                curPt->SetLength(slots);
                const double mae = MeanAbsDiff(stdPt->GetCKKSPackedValue(), curPt->GetCKKSPackedValue());
                out << op << ',' << mode << ",ok," << ms << ',' << mae << ",\"\"" << ',' << slots << ',' << ringDim
                    << '\n';
            }
            catch (const std::exception& e) {
                out << op << ',' << mode << ",error,0.0,0.0,\"" << e.what() << "\"," << slots << ',' << ringDim
                    << '\n';
            }
        }
    }

    std::cout << "wrote " << outCsv << std::endl;
    return 0;
}
