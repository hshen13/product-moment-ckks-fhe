#include "openfhe.h"
#include "gpu/Utils.h"

#include <chrono>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
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

bool ReadEnvBool(const char* name, bool fallback) {
    const char* value = std::getenv(name);
    if (!value) {
        return fallback;
    }
    const std::string s(value);
    return !(s == "0" || s == "false" || s == "False" || s == "FALSE");
}

void SetModeEnv(const std::string& mode, double keepProb, bool safeBootstrap) {
    setenv("OPENFHE_CCFA_MODE", mode.c_str(), 1);
    setenv("OPENFHE_CCFA_KEEP_PROB", std::to_string(keepProb).c_str(), 1);
    setenv("OPENFHE_CCFA_MIN_M", "2", 1);
    setenv("OPENFHE_CCFA_MAX_M", "16", 1);
    setenv("OPENFHE_CCFA_SEED", "1", 1);
    if (safeBootstrap) {
        setenv("OPENFHE_CCFA_SAFE_BOOTSTRAP", "1", 1);
        setenv("OPENFHE_CCFA_SAFE_MAX_M", "2", 1);
        setenv("OPENFHE_CCFA_SAFE_DISABLE_CU", "1", 1);
        if (const char* topM = std::getenv("OPENFHE_CCFA_BOOT_SAFE_DETERMINISTIC_MIN_M")) {
            setenv("OPENFHE_CCFA_SAFE_DETERMINISTIC_MIN_M", topM, 1);
        }
    }
    else {
        unsetenv("OPENFHE_CCFA_SAFE_BOOTSTRAP");
        unsetenv("OPENFHE_CCFA_SAFE_MAX_M");
        unsetenv("OPENFHE_CCFA_SAFE_DISABLE_CU");
        unsetenv("OPENFHE_CCFA_SAFE_DETERMINISTIC_MIN_M");
    }
}

void ClearModeEnv() {
    setenv("OPENFHE_CCFA_MODE", "none", 1);
    unsetenv("OPENFHE_CCFA_KEEP_PROB");
    unsetenv("OPENFHE_CCFA_MIN_M");
    unsetenv("OPENFHE_CCFA_MAX_M");
    unsetenv("OPENFHE_CCFA_SEED");
    unsetenv("OPENFHE_CCFA_SAFE_BOOTSTRAP");
    unsetenv("OPENFHE_CCFA_SAFE_MAX_M");
    unsetenv("OPENFHE_CCFA_SAFE_DISABLE_CU");
    unsetenv("OPENFHE_CCFA_SAFE_DETERMINISTIC_MIN_M");
}

std::vector<double> GaussianNoise(uint32_t n, double sigma, uint32_t seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> dist(0.0, sigma);
    std::vector<double> out(n);
    for (uint32_t i = 0; i < n; ++i) {
        out[i] = dist(rng);
    }
    return out;
}

struct Runtime {
    CryptoContext<DCRTPoly> cc;
    KeyPair<DCRTPoly> kp;
    uint32_t depth = 0;
    uint32_t slots = 0;
    bool useGPU = false;
    std::unique_ptr<ckks::Context> gpuContext;
    std::unique_ptr<ckks::EvaluationKey> gpuEvalKey;
    std::map<uint32_t, ckks::EvaluationKey> gpuRotKeys;
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
    rt.useGPU = ReadEnvUInt("OPENFHE_CCFA_USE_GPU", 1) != 0;
    if (rt.useGPU) {
        const auto cryptoParams =
            std::dynamic_pointer_cast<CryptoParametersCKKSRNS>(rt.cc->GetCryptoParameters());
        rt.gpuContext = std::make_unique<ckks::Context>(GenGPUContext(cryptoParams));
        rt.gpuContext->EnableMemoryPool();
        rt.gpuEvalKey = std::make_unique<ckks::EvaluationKey>(LoadEvalMultRelinKey(rt.cc));
        rt.gpuContext->preloaded_evaluation_key = rt.gpuEvalKey.get();
        const auto evalKeys = rt.cc->GetEvalAutomorphismKeyMap(rt.kp.publicKey->GetKeyTag());
        for (const auto& pair : evalKeys) {
            rt.gpuRotKeys[std::get<0>(pair)] = LoadRelinKey(std::get<1>(pair));
        }
        rt.gpuContext->preloaded_rotation_key_map = &rt.gpuRotKeys;
    }
    return rt;
}

std::vector<double> DecryptReal(CryptoContext<DCRTPoly> cc, const PrivateKey<DCRTPoly>& sk,
                                ConstCiphertext<DCRTPoly> ct, size_t length) {
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

struct ReferenceBatch {
    std::vector<double> h1;
    std::vector<double> h2;
    std::vector<double> logit;
    std::vector<int> y;
};

ReferenceBatch MakeReferenceCompact(const std::vector<double>& x1, const std::vector<double>& x2, double hiddenMix,
                                    double wh1, double wh2, double wout, double bias, double decisionMargin) {
    ReferenceBatch ref;
    const size_t n = x1.size();
    ref.h1.resize(n);
    ref.h2.resize(n);
    ref.logit.resize(n);
    ref.y.resize(n);
    for (size_t i = 0; i < n; ++i) {
        ref.h1[i] = x1[i] * x2[i];
        const double lin = hiddenMix * x1[i] + (1.0 - hiddenMix) * x2[i];
        ref.h2[i] = lin * lin;
        ref.logit[i] = wh1 * ref.h1[i] + wh2 * ref.h2[i] + wout * (ref.h1[i] * ref.h2[i]) + bias;
        ref.y[i] = (ref.logit[i] > decisionMargin) ? 1 : 0;
    }
    return ref;
}

struct Row {
    std::string mode;
    uint32_t dataSeed = 0;
    bool success = false;
    bool finite = false;
    double latencyMs = 0.0;
    double h1Mae = std::numeric_limits<double>::quiet_NaN();
    double h2Mae = std::numeric_limits<double>::quiet_NaN();
    double logitMae = std::numeric_limits<double>::quiet_NaN();
    double logitMse = std::numeric_limits<double>::quiet_NaN();
    double accuracy = std::numeric_limits<double>::quiet_NaN();
    std::string error;
};

Row RunOne(const Runtime& rt, const std::string& mode, double keepProb, bool safeBootstrap, uint32_t dataSeed,
           uint32_t sampleCount, double inputAbsMax, double hiddenMix, double wh1, double wh2, double wout,
           double bias, double decisionMargin) {
    Row row;
    row.mode = mode;
    row.dataSeed = dataSeed;
    try {
        SetModeEnv(mode, keepProb, safeBootstrap);
        setenv("OPENFHE_CCFA_SEED", std::to_string(dataSeed).c_str(), 1);

        std::mt19937 rng(dataSeed);
        std::uniform_real_distribution<double> dist(-inputAbsMax, inputAbsMax);
        std::vector<double> x1(sampleCount), x2(sampleCount);
        for (uint32_t i = 0; i < sampleCount; ++i) {
            x1[i] = dist(rng);
            x2[i] = dist(rng);
        }
        const auto ref = MakeReferenceCompact(x1, x2, hiddenMix, wh1, wh2, wout, bias, decisionMargin);

        auto cc = rt.cc;
        const auto& kp = rt.kp;
        Plaintext pt1 = cc->MakeCKKSPackedPlaintext(x1, 1, rt.depth - 1, nullptr, rt.slots);
        Plaintext pt2 = cc->MakeCKKSPackedPlaintext(x2, 1, rt.depth - 1, nullptr, rt.slots);
        pt1->SetLength(sampleCount);
        pt2->SetLength(sampleCount);
        auto ct1 = cc->Encrypt(kp.publicKey, pt1);
        auto ct2 = cc->Encrypt(kp.publicKey, pt2);
        ct1->SetSlots(rt.slots);
        ct2->SetSlots(rt.slots);

        const auto start = std::chrono::high_resolution_clock::now();

        auto b1 = rt.useGPU ? cc->EvalBootstrapGPU(ct1, *rt.gpuContext) : cc->EvalBootstrap(ct1);
        auto b2 = rt.useGPU ? cc->EvalBootstrapGPU(ct2, *rt.gpuContext) : cc->EvalBootstrap(ct2);

        if (ReadEnvBool("OPENFHE_CCFA_NOISE_FLOOD_ENABLE", false)) {
            const double sigma = ReadEnvDouble("OPENFHE_CCFA_NOISE_FLOOD_SIGMA", 1e-6);
            Plaintext n1 = cc->MakeCKKSPackedPlaintext(GaussianNoise(sampleCount, sigma, dataSeed * 17 + 1), 1,
                                                       rt.depth - 1, nullptr, rt.slots);
            Plaintext n2 = cc->MakeCKKSPackedPlaintext(GaussianNoise(sampleCount, sigma, dataSeed * 17 + 2), 1,
                                                       rt.depth - 1, nullptr, rt.slots);
            n1->SetLength(sampleCount);
            n2->SetLength(sampleCount);
            auto ctN1 = cc->Encrypt(kp.publicKey, n1);
            auto ctN2 = cc->Encrypt(kp.publicKey, n2);
            ctN1->SetSlots(rt.slots);
            ctN2->SetSlots(rt.slots);
            b1 = cc->EvalAdd(b1, ctN1);
            b2 = cc->EvalAdd(b2, ctN2);
        }

        auto h1 = cc->EvalMult(b1, b2);
        auto lin = cc->EvalAdd(cc->EvalMult(b1, hiddenMix), cc->EvalMult(b2, 1.0 - hiddenMix));
        auto h2 = cc->EvalMult(lin, lin);
        auto out = cc->EvalAdd(cc->EvalAdd(cc->EvalMult(h1, wh1), cc->EvalMult(h2, wh2)), cc->EvalMult(cc->EvalMult(h1, h2), wout));
        out = cc->EvalAdd(out, bias);

        const auto stop = std::chrono::high_resolution_clock::now();
        row.latencyMs = std::chrono::duration<double, std::milli>(stop - start).count();

        auto h1Out = DecryptReal(cc, kp.secretKey, h1, sampleCount);
        auto h2Out = DecryptReal(cc, kp.secretKey, h2, sampleCount);
        auto logits = DecryptReal(cc, kp.secretKey, out, sampleCount);

        row.finite = true;
        for (double v : logits) {
            if (!std::isfinite(v)) {
                row.finite = false;
                break;
            }
        }
        row.success = row.finite;
        if (row.finite) {
            row.h1Mae = MeanAbs(h1Out, ref.h1);
            row.h2Mae = MeanAbs(h2Out, ref.h2);
            row.logitMae = MeanAbs(logits, ref.logit);
            row.logitMse = MeanSq(logits, ref.logit);
            uint32_t correct = 0;
            for (uint32_t i = 0; i < sampleCount; ++i) {
                const int pred = (logits[i] > decisionMargin) ? 1 : 0;
                if (pred == ref.y[i]) {
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
    const uint32_t ringDim = ReadEnvUInt("OPENFHE_CCFA_BOOT_RING_DIM", 1u << 12);
    const uint32_t dcrtBits = ReadEnvUInt("OPENFHE_CCFA_BOOT_DCRT_BITS", 59);
    const uint32_t firstMod = ReadEnvUInt("OPENFHE_CCFA_BOOT_FIRST_MOD", 60);
    const uint32_t levelsAfter = ReadEnvUInt("OPENFHE_CCFA_BOOT_LEVELS_AFTER", 10);
    const uint32_t slotsOverride = ReadEnvUInt("OPENFHE_CCFA_BOOT_SLOTS", 0);
    const uint32_t levelBudget0 = ReadEnvUInt("OPENFHE_CCFA_BOOT_LEVEL_BUDGET0", 4);
    const uint32_t levelBudget1 = ReadEnvUInt("OPENFHE_CCFA_BOOT_LEVEL_BUDGET1", 4);
    const double keepProb = ReadEnvDouble("OPENFHE_CCFA_BOOT_KEEP_PROB", 0.6);
    const uint32_t seedStart = ReadEnvUInt("OPENFHE_CCFA_DATA_SEED_START", 1);
    const uint32_t seedEnd = ReadEnvUInt("OPENFHE_CCFA_DATA_SEED_END", 3);
    const uint32_t sampleCount = ReadEnvUInt("OPENFHE_CCFA_SAMPLE_COUNT", 8);
    const double inputAbsMax = ReadEnvDouble("OPENFHE_CCFA_INPUT_ABS_MAX", 1.0);
    const double decisionMargin = ReadEnvDouble("OPENFHE_CCFA_DECISION_MARGIN", 0.0);
    const double hiddenMix = ReadEnvDouble("OPENFHE_CCFA_MLP2_HIDDEN_MIX", 0.65);
    const double wh1 = ReadEnvDouble("OPENFHE_CCFA_MLP2_WH1", 0.7);
    const double wh2 = ReadEnvDouble("OPENFHE_CCFA_MLP2_WH2", -0.4);
    const double wout = ReadEnvDouble("OPENFHE_CCFA_MLP2_WOUT", 1.5);
    const double bias = ReadEnvDouble("OPENFHE_CCFA_MLP2_BIAS", -0.05);
    const std::string outCsv =
        ReadEnvString("OPENFHE_CCFA_HE_MLP2_OUTPUT", "/workspace/results/openfhe_ccfa_he_mlp2_gpu.csv");
    const std::string singleMode = ReadEnvString("OPENFHE_CCFA_HE_MLP2_SINGLE", "");

    std::filesystem::create_directories(std::filesystem::path(outCsv).parent_path());
    std::cerr << "[he-mlp2-gpu] setup\n";
    auto rt = SetupRuntime(ringDim, dcrtBits, firstMod, levelsAfter, slotsOverride, {levelBudget0, levelBudget1});
    std::cerr << "[he-mlp2-gpu] setup-done\n";

    std::ofstream out(outCsv);
    out << "mode,data_seed,success,finite,latency_ms,h1_mae,h2_mae,logit_mae,logit_mse,accuracy,error\n";
    out << std::scientific << std::setprecision(12);
    for (uint32_t seed = seedStart; seed <= seedEnd; ++seed) {
        for (const auto& cfg : std::vector<std::tuple<std::string, double, bool>>{
                 {"deterministic", 1.0, false}, {"independent", keepProb, false}, {"product_safe", keepProb, true}}) {
            if (!singleMode.empty() && singleMode != std::get<0>(cfg)) {
                continue;
            }
            std::cerr << "[he-mlp2-gpu] seed=" << seed << " mode=" << std::get<0>(cfg) << "\n";
            Row row = RunOne(rt, std::get<0>(cfg), std::get<1>(cfg), std::get<2>(cfg), seed, sampleCount, inputAbsMax,
                             hiddenMix, wh1, wh2, wout, bias, decisionMargin);
            out << row.mode << ',' << row.dataSeed << ',' << (row.success ? 1 : 0) << ',' << (row.finite ? 1 : 0) << ','
                << row.latencyMs << ',' << row.h1Mae << ',' << row.h2Mae << ',' << row.logitMae << ',' << row.logitMse
                << ',' << row.accuracy << ",\"" << row.error << "\"\n";
            out.flush();
            std::cerr << "[he-mlp2-gpu] seed=" << seed << " mode=" << std::get<0>(cfg) << " done\n";
        }
    }
    std::cout << "Wrote results to " << outCsv << std::endl;
    return 0;
}
