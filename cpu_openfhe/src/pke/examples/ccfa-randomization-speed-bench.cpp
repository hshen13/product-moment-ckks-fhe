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
    unsetenv("OPENFHE_CCFA_PROFILE_OUT");
}

struct Variant {
    std::string name;
    std::string mode;
    std::string coupling;
    double keepProb = 1.0;
};

struct Runtime {
    CryptoContext<DCRTPoly> cc;
    KeyPair<DCRTPoly> kp;
    uint32_t depth = 0;
    uint32_t slots = 0;
};

std::string ProfilePath(const std::string& dir, const Variant& variant, uint32_t seed, uint32_t rep) {
    if (dir.empty()) {
        return "";
    }
    std::filesystem::create_directories(dir);
    return (std::filesystem::path(dir) / (variant.name + "_seed" + std::to_string(seed) + "_rep" +
                                          std::to_string(rep) + ".profile.csv"))
        .string();
}

void SetVariantEnv(const Variant& variant, uint32_t seed, const std::string& profilePath) {
    ClearCcfaEnv();
    if (!profilePath.empty()) {
        setenv("OPENFHE_CCFA_PROFILE_OUT", profilePath.c_str(), 1);
    }
    if (variant.mode == "none") {
        return;
    }
    setenv("OPENFHE_CCFA_MODE", variant.mode.c_str(), 1);
    setenv("OPENFHE_CCFA_DIST", "bernoulli", 1);
    setenv("OPENFHE_CCFA_COUPLING", variant.coupling.c_str(), 1);
    setenv("OPENFHE_CCFA_KEEP_PROB", std::to_string(variant.keepProb).c_str(), 1);
    setenv("OPENFHE_CCFA_MIN_SCALE", std::to_string(ReadEnvDouble("OPENFHE_CCFA_L1_MIN_SCALE", 0.0)).c_str(), 1);
    setenv("OPENFHE_CCFA_SEED", std::to_string(seed).c_str(), 1);
    setenv("OPENFHE_CCFA_MIN_M", "2", 1);
    setenv("OPENFHE_CCFA_MAX_M", "16", 1);
    const uint32_t protectHead = ReadEnvUInt("OPENFHE_CCFA_L1_PROTECT_HEAD", ReadEnvUInt("OPENFHE_CCFA_L1_PROTECT", 8));
    const uint32_t protectTail = ReadEnvUInt("OPENFHE_CCFA_L1_PROTECT_TAIL", ReadEnvUInt("OPENFHE_CCFA_L1_PROTECT", 8));
    const double eligibleRelAbs = ReadEnvDouble("OPENFHE_CCFA_L1_ELIGIBLE_REL_ABS", 0.003);
    setenv("OPENFHE_CCFA_PROTECT_HEAD", std::to_string(protectHead).c_str(), 1);
    setenv("OPENFHE_CCFA_PROTECT_TAIL", std::to_string(protectTail).c_str(), 1);
    setenv("OPENFHE_CCFA_ELIGIBLE_REL_ABS", std::to_string(eligibleRelAbs).c_str(), 1);
}

Runtime SetupOfficialAdvanced(uint32_t ringDim, uint32_t numSlots, uint32_t levelsAfter) {
    Runtime rt;
    CCParams<CryptoContextCKKSRNS> parameters;
    SecretKeyDist secretKeyDist = UNIFORM_TERNARY;
    parameters.SetSecretKeyDist(secretKeyDist);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(ringDim);
    parameters.SetNumLargeDigits(3);
    parameters.SetKeySwitchTechnique(HYBRID);
    parameters.SetScalingTechnique(FLEXIBLEAUTO);
    parameters.SetScalingModSize(ReadEnvUInt("OPENFHE_CCFA_L1_DCRT_BITS", 59));
    parameters.SetFirstModSize(ReadEnvUInt("OPENFHE_CCFA_L1_FIRST_MOD", 60));

    std::vector<uint32_t> levelBudget = {ReadEnvUInt("OPENFHE_CCFA_L1_LEVEL_BUDGET_ENC", 3),
                                         ReadEnvUInt("OPENFHE_CCFA_L1_LEVEL_BUDGET_DEC", 3)};
    rt.depth = levelsAfter + FHECKKSRNS::GetBootstrapDepth(levelBudget, secretKeyDist);
    parameters.SetMultiplicativeDepth(rt.depth);

    rt.cc = GenCryptoContext(parameters);
    rt.cc->Enable(PKE);
    rt.cc->Enable(KEYSWITCH);
    rt.cc->Enable(LEVELEDSHE);
    rt.cc->Enable(ADVANCEDSHE);
    rt.cc->Enable(FHE);
    rt.slots = numSlots;
    rt.cc->EvalBootstrapSetup(levelBudget, {0, 0}, rt.slots);
    rt.kp = rt.cc->KeyGen();
    rt.cc->EvalMultKeyGen(rt.kp.secretKey);
    rt.cc->EvalBootstrapKeyGen(rt.kp.secretKey, rt.slots);
    return rt;
}

std::vector<double> MakeInput(uint32_t n, uint32_t seed) {
    std::mt19937 gen(seed);
    std::uniform_real_distribution<double> dis(0.0, 1.0);
    std::vector<double> values(n);
    for (auto& value : values) {
        value = dis(gen);
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

void WriteFailure(std::ofstream& out, const Variant& variant, uint32_t seed, uint32_t rep, uint32_t ringDim,
                  uint32_t slots, const std::string& profilePath, const std::string& error) {
    out << variant.name << ',' << seed << ',' << rep << ",0,nan,nan,nan,nan,nan," << ringDim << ',' << slots
        << ",\"" << profilePath << "\",\"" << error << "\"\n";
}

void RunOne(std::ofstream& out, const Variant& variant, uint32_t seed, uint32_t rep, uint32_t ringDim, uint32_t slots,
            uint32_t levelsAfter, const std::string& profileDir) {
    const std::string profilePath = ProfilePath(profileDir, variant, seed, rep);
    SetVariantEnv(variant, seed + 1000003U * rep, profilePath);
    Runtime rt = SetupOfficialAdvanced(ringDim, slots, levelsAfter);
    auto input = MakeInput(slots, seed);
    Plaintext pt = rt.cc->MakeCKKSPackedPlaintext(input, 1, rt.depth - 1, nullptr, rt.slots);
    pt->SetLength(input.size());
    auto ct = rt.cc->Encrypt(rt.kp.publicKey, pt);

    const auto t0 = std::chrono::steady_clock::now();
    auto boot = rt.cc->EvalBootstrap(ct);
    const auto t1 = std::chrono::steady_clock::now();
    const double latencyMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    try {
    auto values = DecryptReal(rt, boot, input.size());
    double maxAbs = 0.0;
    double mae = 0.0;
    double signedMean = 0.0;
    for (size_t i = 0; i < input.size(); ++i) {
        const double err = values[i] - input[i];
        maxAbs = std::max(maxAbs, std::abs(err));
        mae += std::abs(err);
        signedMean += err;
    }
    mae /= static_cast<double>(input.size());
    signedMean /= static_cast<double>(input.size());

    out << variant.name << ',' << seed << ',' << rep << ",1," << latencyMs << ',' << PrecisionBits(maxAbs) << ','
        << maxAbs << ',' << mae << ',' << signedMean << ',' << ringDim << ',' << slots << ",\"" << profilePath
        << "\",\"\"\n";
    }
    catch (const std::exception& e) {
        out << variant.name << ',' << seed << ',' << rep << ",0," << latencyMs << ",nan,nan,nan,nan," << ringDim
            << ',' << slots << ",\"" << profilePath << "\",\"" << e.what() << "\"\n";
    }
    ClearCcfaEnv();
}

}  // namespace

int main() {
    const uint32_t ringDim = ReadEnvUInt("OPENFHE_CCFA_L1_RING_DIM", 1u << 12);
    const uint32_t slots = ReadEnvUInt("OPENFHE_CCFA_L1_SLOTS", 8);
    const uint32_t levelsAfter = ReadEnvUInt("OPENFHE_CCFA_L1_LEVELS_AFTER", 10);
    const uint32_t seeds = ReadEnvUInt("OPENFHE_CCFA_L1_SEEDS", 5);
    const uint32_t reps = ReadEnvUInt("OPENFHE_CCFA_L1_REPS", 2);
    const uint32_t firstSeed = ReadEnvUInt("OPENFHE_CCFA_L1_FIRST_SEED", 1);
    const double keepProb = ReadEnvDouble("OPENFHE_CCFA_L1_KEEP_PROB", 0.5);
    const std::string outCsv =
        ReadEnvString("OPENFHE_CCFA_L1_OUTPUT", "/workspace/results/ccs2026_layered/l1_randomization_speed.csv");
    const std::string profileDir =
        ReadEnvString("OPENFHE_CCFA_L1_PROFILE_DIR", "/workspace/results/ccs2026_layered/l1_profiles");

    std::vector<Variant> variants = {
        {"deterministic", "none", "independent", 1.0},
        {"independent_p050", "independent", "independent", keepProb},
        {"product_coupled_p050", "product", "product_coupled", keepProb},
    };

    const std::string only = ReadEnvString("OPENFHE_CCFA_L1_ONLY", "");
    if (!only.empty()) {
        variants.erase(std::remove_if(variants.begin(), variants.end(),
                                      [&](const Variant& v) { return v.name.find(only) == std::string::npos; }),
                       variants.end());
    }

    std::filesystem::create_directories(std::filesystem::path(outCsv).parent_path());
    std::ofstream out(outCsv);
    out << "variant,seed,rep,success,latency_ms,precision_bits,max_abs_error,mae,mean_signed_error,ring_dim,slots,"
           "profile_path,error\n";
    out << std::scientific << std::setprecision(12);

    for (uint32_t seedOffset = 0; seedOffset < seeds; ++seedOffset) {
        const uint32_t seed = firstSeed + seedOffset;
        for (uint32_t rep = 1; rep <= reps; ++rep) {
            for (const auto& variant : variants) {
                try {
                    RunOne(out, variant, seed, rep, ringDim, slots, levelsAfter, profileDir);
                }
                catch (const std::exception& e) {
                    WriteFailure(out, variant, seed, rep, ringDim, slots, "", e.what());
                    ClearCcfaEnv();
                }
            }
        }
    }

    std::cout << "Wrote Layer 1 randomization speed results to " << outCsv << std::endl;
    return 0;
}
