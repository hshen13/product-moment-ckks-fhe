#include "openfhe.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
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

void SetProductCoupledEnv(uint32_t seed) {
    const double keepProb = ReadEnvDouble("OPENFHE_CCFA_TUNED_KEEP_PROB", 0.91);
    const double minScale = ReadEnvDouble("OPENFHE_CCFA_TUNED_MIN_SCALE", 0.992);
    const uint32_t protect = ReadEnvUInt("OPENFHE_CCFA_TUNED_PROTECT", 8);
    const double relAbs = ReadEnvDouble("OPENFHE_CCFA_TUNED_ELIGIBLE_REL_ABS", 0.003);

    setenv("OPENFHE_CCFA_MODE", "product_safe", 1);
    setenv("OPENFHE_CCFA_DIST", "bernoulli", 1);
    setenv("OPENFHE_CCFA_COUPLING", "product_coupled", 1);
    setenv("OPENFHE_CCFA_KEEP_PROB", std::to_string(keepProb).c_str(), 1);
    setenv("OPENFHE_CCFA_MIN_SCALE", std::to_string(minScale).c_str(), 1);
    setenv("OPENFHE_CCFA_SEED", std::to_string(seed).c_str(), 1);
    setenv("OPENFHE_CCFA_MIN_M", "2", 1);
    setenv("OPENFHE_CCFA_MAX_M", "16", 1);
    setenv("OPENFHE_CCFA_SAFE_BOOTSTRAP", "1", 1);
    setenv("OPENFHE_CCFA_SAFE_MAX_M", "2", 1);
    setenv("OPENFHE_CCFA_SAFE_DISABLE_CU", "1", 1);
    setenv("OPENFHE_CCFA_PROTECT_HEAD", std::to_string(protect).c_str(), 1);
    setenv("OPENFHE_CCFA_PROTECT_TAIL", std::to_string(protect).c_str(), 1);
    setenv("OPENFHE_CCFA_ELIGIBLE_REL_ABS", std::to_string(relAbs).c_str(), 1);
}

struct Variant {
    std::string family;
    bool composite = false;
    bool productCoupled = false;
    uint32_t numIterations = 1;
    uint32_t precisionHint = 0;
    uint32_t dcrtBits = 59;
    uint32_t firstMod = 60;
    uint32_t registerWordSize = 64;
    std::vector<uint32_t> levelBudget = {4, 4};
};

struct Runtime {
    CryptoContext<DCRTPoly> cc;
    KeyPair<DCRTPoly> kp;
    uint32_t depth = 0;
    uint32_t slots = 0;
    uint32_t compositeDegree = 1;
};

Runtime SetupRuntime(const Variant& variant, uint32_t ringDim, uint32_t levelsAfter, uint32_t slotsOverride) {
    Runtime rt;
    CCParams<CryptoContextCKKSRNS> parameters;
    SecretKeyDist secretKeyDist = UNIFORM_TERNARY;
    parameters.SetSecretKeyDist(secretKeyDist);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(ringDim);
    parameters.SetScalingTechnique(variant.composite ? COMPOSITESCALINGAUTO : FLEXIBLEAUTO);
    parameters.SetScalingModSize(variant.dcrtBits);
    parameters.SetFirstModSize(variant.firstMod);
    if (variant.composite) {
        parameters.SetRegisterWordSize(variant.registerWordSize);
    }

    rt.depth = levelsAfter + FHECKKSRNS::GetBootstrapDepth(variant.levelBudget, secretKeyDist) +
               (variant.numIterations > 1 ? variant.numIterations - 1 : 0);
    parameters.SetMultiplicativeDepth(rt.depth);

    rt.cc = GenCryptoContext(parameters);
    rt.cc->Enable(PKE);
    rt.cc->Enable(KEYSWITCH);
    rt.cc->Enable(LEVELEDSHE);
    rt.cc->Enable(ADVANCEDSHE);
    rt.cc->Enable(FHE);

    const auto cryptoParams = std::dynamic_pointer_cast<CryptoParametersCKKSRNS>(rt.cc->GetCryptoParameters());
    rt.compositeDegree = cryptoParams->GetCompositeDegree();
    rt.slots = (slotsOverride == 0) ? (rt.cc->GetRingDimension() / 2) : slotsOverride;

    rt.cc->EvalBootstrapSetup(variant.levelBudget, {0, 0}, rt.slots);
    rt.kp = rt.cc->KeyGen();
    rt.cc->EvalMultKeyGen(rt.kp.secretKey);
    rt.cc->EvalBootstrapKeyGen(rt.kp.secretKey, rt.slots);
    return rt;
}

std::vector<double> MakeInput(uint32_t n, double bound, uint32_t seed) {
    std::mt19937 gen(seed);
    std::uniform_real_distribution<double> jitter(-bound / 16.0, bound / 16.0);
    std::vector<double> values(n);
    for (uint32_t i = 0; i < n; ++i) {
        const double phase = 2.0 * M_PI * static_cast<double>(i) / static_cast<double>(std::max<uint32_t>(n, 1));
        values[i] = bound * (0.65 * std::sin(phase) + 0.25 * std::cos(3.0 * phase)) + jitter(gen);
    }
    return values;
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

double PrecisionBits(double maxAbs) {
    if (maxAbs <= 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    return -std::log2(maxAbs);
}

void WriteFailure(std::ofstream& out, const Variant& variant, uint32_t seed, uint32_t ringDim, uint32_t slotsOverride,
                  const std::string& error) {
    out << variant.family << ',' << (variant.productCoupled ? "product_coupled" : "none") << ',' << seed
        << ",0,nan,nan,nan,nan,nan,nan,nan," << ringDim << ',' << slotsOverride << ',' << variant.numIterations
        << ',' << variant.precisionHint << ',' << (variant.composite ? "composite" : "default") << ",\"" << error
        << "\"\n";
}

void RunOne(std::ofstream& out, const Variant& variant, uint32_t ringDim, uint32_t levelsAfter, uint32_t slotsOverride,
            uint32_t inputLengthOverride, double inputBound, uint32_t seed) {
    ClearCcfaEnv();
    if (variant.productCoupled) {
        SetProductCoupledEnv(seed);
    }

    Runtime rt = SetupRuntime(variant, ringDim, levelsAfter, slotsOverride);
    const uint32_t inputLength = inputLengthOverride == 0 ? rt.slots : std::min<uint32_t>(inputLengthOverride, rt.slots);
    const auto input = MakeInput(inputLength, inputBound, seed);
    const uint32_t startLevel = variant.composite ? rt.compositeDegree * (rt.depth - 1) : rt.depth - 1;
    Plaintext pt = rt.cc->MakeCKKSPackedPlaintext(input, 1, startLevel, nullptr, rt.slots);
    pt->SetLength(input.size());

    auto ct = rt.cc->Encrypt(rt.kp.publicKey, pt);
    ct->SetSlots(rt.slots);

    const auto t0 = std::chrono::steady_clock::now();
    auto boot = rt.cc->EvalBootstrap(ct, variant.numIterations, variant.precisionHint);
    const auto t1 = std::chrono::steady_clock::now();
    const double latencyMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    auto values = DecryptReal(rt, boot, input.size());
    double maxAbs = 0.0;
    double mae = 0.0;
    double mse = 0.0;
    double meanSigned = 0.0;
    for (size_t i = 0; i < input.size(); ++i) {
        const double err = values[i] - input[i];
        maxAbs = std::max(maxAbs, std::abs(err));
        mae += std::abs(err);
        mse += err * err;
        meanSigned += err;
    }
    mae /= static_cast<double>(input.size());
    mse /= static_cast<double>(input.size());
    meanSigned /= static_cast<double>(input.size());

    const double levelScale = variant.composite ? static_cast<double>(rt.compositeDegree) : 1.0;
    const double levelsRemaining =
        static_cast<double>(rt.depth) - static_cast<double>(boot->GetLevel()) / levelScale -
        static_cast<double>(boot->GetNoiseScaleDeg() - 1);

    out << variant.family << ',' << (variant.productCoupled ? "product_coupled" : "none") << ',' << seed << ",1,"
        << latencyMs << ',' << PrecisionBits(maxAbs) << ',' << maxAbs << ',' << mae << ',' << mse << ','
        << meanSigned << ',' << levelsRemaining << ',' << ringDim << ',' << rt.slots << ',' << variant.numIterations
        << ',' << variant.precisionHint << ',' << (variant.composite ? "composite" : "default") << ",\"\"\n";
}

}  // namespace

int main() {
    const uint32_t ringDim = ReadEnvUInt("OPENFHE_CCFA_E6_RING_DIM", 1u << 12);
    const uint32_t slotsOverride = ReadEnvUInt("OPENFHE_CCFA_E6_SLOTS", 0);
    const uint32_t inputLength = ReadEnvUInt("OPENFHE_CCFA_E6_INPUT_N", 0);
    const uint32_t seeds = ReadEnvUInt("OPENFHE_CCFA_E6_SEEDS", 5);
    const uint32_t firstSeed = ReadEnvUInt("OPENFHE_CCFA_E6_FIRST_SEED", 1);
    const uint32_t levelsAfter = ReadEnvUInt("OPENFHE_CCFA_E6_LEVELS_AFTER", 10);
    const double inputBound = ReadEnvDouble("OPENFHE_CCFA_E6_BOUND", 0.25);
    const std::string outCsv =
        ReadEnvString("OPENFHE_CCFA_E6_OUTPUT", "/workspace/results/ccs2026_e6/sota_bootstrap.csv");

    std::vector<Variant> variants = {
        {"chebyshev_default", false, false, 1, 0, 59, 60, 64, {4, 4}},
        {"chebyshev_default", false, true, 1, 0, 59, 60, 64, {4, 4}},
        {"metabts2", false, false, 2, 17, 59, 60, 64, {3, 3}},
        {"metabts2", false, true, 2, 17, 59, 60, 64, {3, 3}},
        {"composite_scaling", true, false, 1, 11, 98, 100, 64, {4, 4}},
        {"composite_scaling", true, true, 1, 11, 98, 100, 64, {4, 4}},
        {"composite_metabts2", true, false, 2, 19, 61, 66, 27, {3, 3}},
        {"composite_metabts2", true, true, 2, 19, 61, 66, 27, {3, 3}},
    };

    const std::string only = ReadEnvString("OPENFHE_CCFA_E6_ONLY", "");
    if (!only.empty()) {
        variants.erase(std::remove_if(variants.begin(), variants.end(),
                                      [&](const Variant& v) { return v.family.find(only) == std::string::npos; }),
                       variants.end());
    }

    std::filesystem::create_directories(std::filesystem::path(outCsv).parent_path());
    std::ofstream out(outCsv);
    out << "family,randomization,seed,success,latency_ms,precision_bits,max_abs_error,mae,mse,mean_signed_error,"
           "levels_remaining,ring_dim,slots,num_iterations,precision_hint,scaling,error\n";
    out << std::scientific << std::setprecision(12);

    for (uint32_t s = 0; s < seeds; ++s) {
        const uint32_t seed = firstSeed + s;
        for (const auto& variant : variants) {
            try {
                RunOne(out, variant, ringDim, levelsAfter, slotsOverride, inputLength, inputBound, seed);
            }
            catch (const std::exception& e) {
                WriteFailure(out, variant, seed, ringDim, slotsOverride, e.what());
            }
            ClearCcfaEnv();
        }
    }

    std::cout << "Wrote E6 SOTA bootstrap comparison to " << outCsv << std::endl;
    return 0;
}
