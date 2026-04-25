#include "openfhe.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace lbcrypto;

namespace {

uint32_t GetEnvUInt(const char* name, uint32_t fallback) {
    if (const char* raw = std::getenv(name)) {
        return static_cast<uint32_t>(std::strtoul(raw, nullptr, 10));
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
    double total = 0.0;
    for (size_t i = 0; i < n; ++i) {
        total += std::abs(lhs[i] - rhs[i]);
    }
    return (n == 0) ? 0.0 : total / static_cast<double>(n);
}

std::vector<std::complex<double>> DecryptPacked(CryptoContext<DCRTPoly> cc, const PrivateKey<DCRTPoly>& sk,
                                                ConstCiphertext<DCRTPoly> ct, size_t slots) {
    Plaintext dec;
    cc->Decrypt(sk, ct, &dec);
    dec->SetLength(slots);
    return dec->GetCKKSPackedValue();
}

}  // namespace

int main() {
    const uint32_t ringDim = GetEnvUInt("OPENFHE_NC_PROBE_RING_DIM", 1u << 12);
    const uint32_t dcrtBits = GetEnvUInt("OPENFHE_NC_PROBE_DCRT_BITS", 59);
    const uint32_t firstMod = GetEnvUInt("OPENFHE_NC_PROBE_FIRST_MOD", 60);
    const uint32_t levelsAfter = GetEnvUInt("OPENFHE_NC_PROBE_LEVELS_AFTER", 10);
    const uint32_t trials = GetEnvUInt("OPENFHE_NC_LIPSCHITZ_TRIALS", 20);
    const double eps = std::getenv("OPENFHE_NC_LIPSCHITZ_EPS") ? std::stod(std::getenv("OPENFHE_NC_LIPSCHITZ_EPS")) : 1e-4;
    const std::string outCsv = GetEnvString("OPENFHE_NC_LIPSCHITZ_OUTPUT",
                                            "/workspace/results/openfhe_ccfa_shell_lipschitz.csv");

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

    std::ofstream out(outCsv);
    out << "stage,trials,eps,mean_gain,max_gain,p95_gain,ring_dim\n";
    out << std::scientific << std::setprecision(12);

    std::mt19937 rng(17);
    std::uniform_real_distribution<double> dist(-0.25, 0.25);
    std::normal_distribution<double> ndist(0.0, 1.0);

    for (const auto& stage : {std::string("coeffstoslots"), std::string("slotstocoeffs")}) {
        std::vector<double> gains;
        gains.reserve(trials);

        for (uint32_t t = 0; t < trials; ++t) {
            std::vector<std::complex<double>> x(slots), x2(slots), delta(slots);
            double norm = 0.0;
            for (uint32_t i = 0; i < slots; ++i) {
                x[i] = {dist(rng), dist(rng)};
                delta[i] = {ndist(rng), ndist(rng)};
                norm += std::norm(delta[i]);
            }
            norm = std::sqrt(norm);
            for (uint32_t i = 0; i < slots; ++i) {
                delta[i] /= norm;
                delta[i] *= eps;
                x2[i] = x[i] + delta[i];
            }

            auto pt1 = cc->MakeCKKSPackedPlaintext(x, 1, depth - 1, nullptr, slots);
            auto pt2 = cc->MakeCKKSPackedPlaintext(x2, 1, depth - 1, nullptr, slots);
            auto ct1 = cc->Encrypt(kp.publicKey, pt1);
            auto ct2 = cc->Encrypt(kp.publicKey, pt2);

            Ciphertext<DCRTPoly> y1, y2;
            if (stage == "coeffstoslots") {
                y1 = cc->EvalBootstrapProbeCoeffsToSlots(ct1);
                y2 = cc->EvalBootstrapProbeCoeffsToSlots(ct2);
            }
            else {
                y1 = cc->EvalBootstrapProbeSlotsToCoeffs(ct1);
                y2 = cc->EvalBootstrapProbeSlotsToCoeffs(ct2);
            }

            auto dy1 = DecryptPacked(cc, kp.secretKey, y1, slots);
            auto dy2 = DecryptPacked(cc, kp.secretKey, y2, slots);
            double outdiff = MeanAbsDiff(dy1, dy2);
            gains.push_back(outdiff / eps);
        }

        double mean = 0.0;
        double maxv = 0.0;
        for (double g : gains) {
            mean += g;
            maxv = std::max(maxv, g);
        }
        mean /= static_cast<double>(gains.size());
        std::sort(gains.begin(), gains.end());
        const size_t p95Index = std::min(gains.size() - 1, static_cast<size_t>(std::ceil(0.95 * gains.size())) - 1);
        out << stage << ',' << trials << ',' << eps << ',' << mean << ',' << maxv << ',' << gains[p95Index] << ','
            << ringDim << '\n';
    }

    std::cout << "wrote " << outCsv << std::endl;
    return 0;
}
