#include "openfhe.h"

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

void SetModeEnv(const std::string& mode, double keepProb, bool safeBootstrap, uint32_t seed) {
    setenv("OPENFHE_CCFA_MODE", mode.c_str(), 1);
    setenv("OPENFHE_CCFA_KEEP_PROB", std::to_string(keepProb).c_str(), 1);
    setenv("OPENFHE_CCFA_SEED", std::to_string(seed).c_str(), 1);
    setenv("OPENFHE_CCFA_MIN_M", "2", 1);
    setenv("OPENFHE_CCFA_MAX_M", "16", 1);
    if (safeBootstrap) {
        setenv("OPENFHE_CCFA_SAFE_BOOTSTRAP", "1", 1);
        setenv("OPENFHE_CCFA_SAFE_MAX_M", "2", 1);
        setenv("OPENFHE_CCFA_SAFE_DISABLE_CU", "1", 1);
    }
    else {
        unsetenv("OPENFHE_CCFA_SAFE_BOOTSTRAP");
        unsetenv("OPENFHE_CCFA_SAFE_MAX_M");
        unsetenv("OPENFHE_CCFA_SAFE_DISABLE_CU");
    }
}

void ClearModeEnv() {
    setenv("OPENFHE_CCFA_MODE", "none", 1);
    unsetenv("OPENFHE_CCFA_KEEP_PROB");
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
                     uint32_t slotsOverride, const std::vector<uint32_t>& levelBudget) {
    Runtime rt;
    CCParams<CryptoContextCKKSRNS> parameters;
    SecretKeyDist secretKeyDist = UNIFORM_TERNARY;
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

double MeanAbs(const std::vector<double>& a, const std::vector<double>& b) {
    double s = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        s += std::abs(a[i] - b[i]);
    }
    return s / static_cast<double>(a.size());
}

double MeanSq(const std::vector<double>& a, const std::vector<double>& b) {
    double s = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        const double d = a[i] - b[i];
        s += d * d;
    }
    return s / static_cast<double>(a.size());
}

std::vector<double> ReferenceLayerStack(std::vector<double> x, uint32_t layers, double w, double b, double skip) {
    for (uint32_t layer = 0; layer < layers; ++layer) {
        for (double& v : x) {
            const double lin = w * v + b;
            v = lin * lin + skip * v;
        }
    }
    return x;
}

struct Row {
    uint32_t layers = 0;
    std::string mode;
    uint32_t seed = 0;
    bool success = false;
    bool finite = false;
    double latencyMs = 0.0;
    double mae = std::numeric_limits<double>::quiet_NaN();
    double mse = std::numeric_limits<double>::quiet_NaN();
    double accuracy = std::numeric_limits<double>::quiet_NaN();
    std::string error;
};

Row RunOne(const Runtime& rt, uint32_t layers, const std::string& mode, double keepProb, bool safeBootstrap,
           uint32_t seed, uint32_t sampleCount, double inputAbsMax, double w, double b, double skip,
           double decisionMargin) {
    Row row;
    row.layers = layers;
    row.mode = mode;
    row.seed = seed;
    try {
        SetModeEnv(mode, keepProb, safeBootstrap, seed);
        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> dist(-inputAbsMax, inputAbsMax);
        std::vector<double> x(sampleCount);
        for (double& v : x) {
            v = dist(rng);
        }
        const auto ref = ReferenceLayerStack(x, layers, w, b, skip);

        auto cc = rt.cc;
        Plaintext pt = cc->MakeCKKSPackedPlaintext(x, 1, rt.depth - 1, nullptr, rt.slots);
        pt->SetLength(sampleCount);
        auto ct = cc->Encrypt(rt.kp.publicKey, pt);
        ct->SetSlots(rt.slots);

        const auto start = std::chrono::high_resolution_clock::now();
        for (uint32_t layer = 0; layer < layers; ++layer) {
            ct = cc->EvalBootstrap(ct);
            auto lin = cc->EvalAdd(cc->EvalMult(ct, w), b);
            auto sq = cc->EvalMult(lin, lin);
            ct = cc->EvalAdd(sq, cc->EvalMult(ct, skip));
        }
        const auto stop = std::chrono::high_resolution_clock::now();
        row.latencyMs = std::chrono::duration<double, std::milli>(stop - start).count();

        auto out = DecryptReal(cc, rt.kp.secretKey, ct, sampleCount);
        row.finite = true;
        for (double v : out) {
            if (!std::isfinite(v)) {
                row.finite = false;
                break;
            }
        }
        row.success = row.finite;
        if (row.finite) {
            row.mae = MeanAbs(out, ref);
            row.mse = MeanSq(out, ref);
            uint32_t correct = 0;
            for (uint32_t i = 0; i < sampleCount; ++i) {
                const int pred = (out[i] > decisionMargin) ? 1 : 0;
                const int truth = (ref[i] > decisionMargin) ? 1 : 0;
                if (pred == truth) {
                    ++correct;
                }
            }
            row.accuracy = static_cast<double>(correct) / static_cast<double>(sampleCount);
        }
        ClearModeEnv();
    }
    catch (const std::exception& e) {
        ClearModeEnv();
        row.error = e.what();
    }
    return row;
}

}  // namespace

int main() {
    const uint32_t ringDim = ReadEnvUInt("OPENFHE_CCFA_BOOT_RING_DIM", 1u << 16);
    const uint32_t dcrtBits = ReadEnvUInt("OPENFHE_CCFA_BOOT_DCRT_BITS", 59);
    const uint32_t firstMod = ReadEnvUInt("OPENFHE_CCFA_BOOT_FIRST_MOD", 60);
    const uint32_t levelsAfter = ReadEnvUInt("OPENFHE_CCFA_BOOT_LEVELS_AFTER", 20);
    const uint32_t slotsOverride = ReadEnvUInt("OPENFHE_CCFA_BOOT_SLOTS", 1u << 14);
    const uint32_t levelBudget0 = ReadEnvUInt("OPENFHE_CCFA_BOOT_LEVEL_BUDGET0", 4);
    const uint32_t levelBudget1 = ReadEnvUInt("OPENFHE_CCFA_BOOT_LEVEL_BUDGET1", 4);
    const double keepProb = ReadEnvDouble("OPENFHE_CCFA_BOOT_KEEP_PROB", 0.91);
    const uint32_t seedStart = ReadEnvUInt("OPENFHE_CCFA_DATA_SEED_START", 1);
    const uint32_t seedEnd = ReadEnvUInt("OPENFHE_CCFA_DATA_SEED_END", 10);
    const uint32_t sampleCount = ReadEnvUInt("OPENFHE_CCFA_SAMPLE_COUNT", 256);
    const uint32_t maxLayers = ReadEnvUInt("OPENFHE_CCFA_MAX_LAYERS", 8);
    const double inputAbsMax = ReadEnvDouble("OPENFHE_CCFA_INPUT_ABS_MAX", 0.15);
    const double decisionMargin = ReadEnvDouble("OPENFHE_CCFA_DECISION_MARGIN", 0.0);
    const double w = ReadEnvDouble("OPENFHE_CCFA_LAYER_W", 0.8);
    const double b = ReadEnvDouble("OPENFHE_CCFA_LAYER_B", 0.05);
    const double skip = ReadEnvDouble("OPENFHE_CCFA_LAYER_SKIP", 0.15);
    const std::string singleMode = ReadEnvString("OPENFHE_CCFA_MULTI_SINGLE", "");
    const std::string outCsv =
        ReadEnvString("OPENFHE_CCFA_MULTI_OUTPUT", "/workspace/results/ccs2026/multi_layer.csv");

    std::filesystem::create_directories(std::filesystem::path(outCsv).parent_path());
    auto rt = SetupRuntime(ringDim, dcrtBits, firstMod, levelsAfter, slotsOverride, {levelBudget0, levelBudget1});

    std::ofstream out(outCsv);
    out << "layers,mode,data_seed,success,finite,latency_ms,mae,mse,accuracy,error,ring_dim,slots\n";
    out << std::scientific << std::setprecision(12);
    for (uint32_t layers = 2; layers <= maxLayers; layers += 2) {
        for (uint32_t seed = seedStart; seed <= seedEnd; ++seed) {
            for (const auto& cfg : std::vector<std::tuple<std::string, double, bool>>{
                     {"deterministic", 1.0, false}, {"independent", keepProb, false}, {"product_safe", keepProb, true}}) {
                if (!singleMode.empty() && singleMode != std::get<0>(cfg)) {
                    continue;
                }
                Row row = RunOne(rt, layers, std::get<0>(cfg), std::get<1>(cfg), std::get<2>(cfg), seed, sampleCount,
                                 inputAbsMax, w, b, skip, decisionMargin);
                out << row.layers << ',' << row.mode << ',' << row.seed << ',' << (row.success ? 1 : 0) << ','
                    << (row.finite ? 1 : 0) << ',' << row.latencyMs << ',' << row.mae << ',' << row.mse << ','
                    << row.accuracy << ",\"" << row.error << "\"," << ringDim << ',' << rt.slots << '\n';
                out.flush();
            }
        }
    }
    std::cout << "Wrote results to " << outCsv << std::endl;
    return 0;
}
