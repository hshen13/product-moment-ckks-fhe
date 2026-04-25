#include "openfhe.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <tuple>
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

void SetProductSafeEnv(double keepProb, double minScale, uint32_t seed) {
    setenv("OPENFHE_CCFA_MODE", "product_safe", 1);
    setenv("OPENFHE_CCFA_COUPLING", "product_coupled", 1);
    setenv("OPENFHE_CCFA_KEEP_PROB", std::to_string(keepProb).c_str(), 1);
    setenv("OPENFHE_CCFA_MIN_SCALE", std::to_string(minScale).c_str(), 1);
    setenv("OPENFHE_CCFA_SEED", std::to_string(seed).c_str(), 1);
    setenv("OPENFHE_CCFA_MIN_M", "2", 1);
    setenv("OPENFHE_CCFA_MAX_M", "16", 1);
    setenv("OPENFHE_CCFA_SAFE_BOOTSTRAP", "1", 1);
    setenv("OPENFHE_CCFA_SAFE_MAX_M", "2", 1);
    setenv("OPENFHE_CCFA_SAFE_DISABLE_CU", "1", 1);
}

void ClearEnv() {
    setenv("OPENFHE_CCFA_MODE", "none", 1);
    unsetenv("OPENFHE_CCFA_COUPLING");
    unsetenv("OPENFHE_CCFA_KEEP_PROB");
    unsetenv("OPENFHE_CCFA_MIN_SCALE");
    unsetenv("OPENFHE_CCFA_SEED");
    unsetenv("OPENFHE_CCFA_MIN_M");
    unsetenv("OPENFHE_CCFA_MAX_M");
    unsetenv("OPENFHE_CCFA_SAFE_BOOTSTRAP");
    unsetenv("OPENFHE_CCFA_SAFE_MAX_M");
    unsetenv("OPENFHE_CCFA_SAFE_DISABLE_CU");
}

struct Runtime {
    CryptoContext<DCRTPoly> cc;
    KeyPair<DCRTPoly> kp;
    uint32_t depth = 0;
    uint32_t slots = 0;
};

Runtime SetupRuntime(uint32_t ringDim, uint32_t dcrtBits, uint32_t firstMod, uint32_t levelsAfter,
                     uint32_t slotsOverride) {
    Runtime rt;
    CCParams<CryptoContextCKKSRNS> parameters;
    SecretKeyDist secretKeyDist = UNIFORM_TERNARY;
    std::vector<uint32_t> levelBudget = {4, 4};
    parameters.SetSecretKeyDist(secretKeyDist);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(ringDim);
    parameters.SetScalingTechnique(FLEXIBLEAUTO);
    parameters.SetScalingModSize(dcrtBits);
    parameters.SetFirstModSize(firstMod);
    rt.depth = levelsAfter + FHECKKSRNS::GetBootstrapDepth(levelBudget, secretKeyDist);
    parameters.SetMultiplicativeDepth(rt.depth);

    rt.cc = GenCryptoContext(parameters);
    rt.cc->Enable(PKE);
    rt.cc->Enable(KEYSWITCH);
    rt.cc->Enable(LEVELEDSHE);
    rt.cc->Enable(ADVANCEDSHE);
    rt.cc->Enable(FHE);

    rt.slots = (slotsOverride == 0) ? (rt.cc->GetRingDimension() / 2) : slotsOverride;
    rt.cc->EvalBootstrapSetup(levelBudget, {0, 0}, rt.slots);
    rt.kp = rt.cc->KeyGen();
    rt.cc->EvalMultKeyGen(rt.kp.secretKey);
    rt.cc->EvalBootstrapKeyGen(rt.kp.secretKey, rt.slots);
    return rt;
}

std::vector<double> DecryptReal(CryptoContext<DCRTPoly> cc, const PrivateKey<DCRTPoly>& sk,
                                ConstCiphertext<DCRTPoly>& ct, size_t length) {
    Plaintext dec;
    cc->Decrypt(sk, ct, &dec);
    dec->SetLength(length);
    std::vector<double> out;
    out.reserve(length);
    for (const auto& v : dec->GetCKKSPackedValue()) {
        out.push_back(v.real());
    }
    return out;
}

}  // namespace

int main() {
    const uint32_t ringDim = ReadEnvUInt("OPENFHE_CCFA_BOOT_RING_DIM", 1u << 12);
    const uint32_t dcrtBits = ReadEnvUInt("OPENFHE_CCFA_BOOT_DCRT_BITS", 59);
    const uint32_t firstMod = ReadEnvUInt("OPENFHE_CCFA_BOOT_FIRST_MOD", 60);
    const uint32_t levelsAfter = ReadEnvUInt("OPENFHE_CCFA_BOOT_LEVELS_AFTER", 10);
    const uint32_t slotsOverride = ReadEnvUInt("OPENFHE_CCFA_BOOT_SLOTS", 0);
    const uint32_t gridN = ReadEnvUInt("OPENFHE_CCFA_BIAS_GRID_N", 100);
    const uint32_t seed = ReadEnvUInt("OPENFHE_CCFA_BIAS_SEED", 1);
    const double bound = ReadEnvDouble("OPENFHE_CCFA_BIAS_BOUND", 0.25);
    const double decodeMargin = ReadEnvDouble("OPENFHE_CCFA_DECODE_MARGIN", 1e-3);
    const std::string outCsv = ReadEnvString("OPENFHE_CCFA_BIAS_OUTPUT", "/workspace/results/ccs2026/bias.csv");

    std::vector<double> input(gridN);
    for (uint32_t i = 0; i < gridN; ++i) {
        input[i] = -bound + 2.0 * bound * static_cast<double>(i) / static_cast<double>(std::max<uint32_t>(gridN - 1, 1));
    }

    std::filesystem::create_directories(std::filesystem::path(outCsv).parent_path());
    auto rt = SetupRuntime(ringDim, dcrtBits, firstMod, levelsAfter, slotsOverride);
    std::ofstream out(outCsv);
    out << "config,p,min_scale,beta,success,max_abs_bias,mean_bias,std_bias,tolerable,error,ring_dim,slots\n";
    out << std::scientific << std::setprecision(12);

    for (const auto& cfg : std::vector<std::tuple<std::string, double, double>>{
             {"cpu_deployment", 0.60, 0.95}, {"gpu_deployment", 0.91, 0.992}, {"aggressive", 0.30, 0.95}}) {
        const std::string label = std::get<0>(cfg);
        const double p = std::get<1>(cfg);
        const double minScale = std::get<2>(cfg);
        const double beta = (minScale > 0.0) ? p / (minScale * minScale) : std::numeric_limits<double>::quiet_NaN();
        try {
            SetProductSafeEnv(p, minScale, seed);
            Plaintext pt = rt.cc->MakeCKKSPackedPlaintext(input, 1, rt.depth - 1, nullptr, rt.slots);
            pt->SetLength(input.size());
            auto ct = rt.cc->Encrypt(rt.kp.publicKey, pt);
            ct->SetSlots(rt.slots);
            auto boot = rt.cc->EvalBootstrap(ct);
            auto values = DecryptReal(rt.cc, rt.kp.secretKey, boot, input.size());

            double maxAbs = 0.0;
            double mean = 0.0;
            for (size_t i = 0; i < values.size(); ++i) {
                const double bias = values[i] - input[i];
                maxAbs = std::max(maxAbs, std::abs(bias));
                mean += bias;
            }
            mean /= static_cast<double>(values.size());
            double var = 0.0;
            for (size_t i = 0; i < values.size(); ++i) {
                const double centered = (values[i] - input[i]) - mean;
                var += centered * centered;
            }
            const double stddev = std::sqrt(var / static_cast<double>(values.size()));
            out << label << ',' << p << ',' << minScale << ',' << beta << ",1," << maxAbs << ',' << mean << ','
                << stddev << ',' << ((maxAbs < decodeMargin) ? "yes" : "no") << ",\"\"," << ringDim << ','
                << rt.slots << '\n';
        }
        catch (const std::exception& e) {
            out << label << ',' << p << ',' << minScale << ',' << beta << ",0,nan,nan,nan,no,\"" << e.what()
                << "\"," << ringDim << ',' << rt.slots << '\n';
        }
        ClearEnv();
    }
    std::cout << "Wrote results to " << outCsv << std::endl;
    return 0;
}
