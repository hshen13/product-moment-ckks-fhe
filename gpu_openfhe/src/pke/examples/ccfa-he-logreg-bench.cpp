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
    const std::string v(value);
    return v == "1" || v == "true" || v == "TRUE" || v == "yes";
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
    rt.useGPU = ReadEnvUInt("OPENFHE_CCFA_USE_GPU", 0) != 0;
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

std::vector<double> EvalReference(const std::vector<double>& x1, const std::vector<double>& x2, double w1, double w2,
                                  double winter, double bias, uint32_t productPower,
                                  const std::vector<double>& x3 = {}, const std::vector<double>& x4 = {}) {
    std::vector<double> out(x1.size(), 0.0);
    for (size_t i = 0; i < x1.size(); ++i) {
        double prod = x1[i] * x2[i];
        if (productPower >= 2 && !x3.empty()) {
            prod *= x3[i];
        }
        if (productPower >= 3 && !x4.empty()) {
            prod *= x4[i];
        }
        out[i] = w1 * x1[i] + w2 * x2[i] + winter * prod + bias;
    }
    return out;
}

struct Row {
    std::string mode;
    uint32_t dataSeed = 0;
    bool success = false;
    bool finite = false;
    double latencyMs = 0.0;
    double logitMae = std::numeric_limits<double>::quiet_NaN();
    double logitMse = std::numeric_limits<double>::quiet_NaN();
    double accuracy = std::numeric_limits<double>::quiet_NaN();
    std::string error;
};

void AppendStageRow(const std::string& path, const std::string& backend, const std::string& mode, uint32_t dataSeed,
                    const std::string& stage, const std::vector<double>& out, const std::vector<double>& ref) {
    if (path.empty()) {
        return;
    }
    const bool exists = std::filesystem::exists(path);
    std::ofstream f(path, std::ios::app);
    if (!exists) {
        f << "backend,mode,data_seed,stage,mae,mse\n";
        f << std::scientific << std::setprecision(12);
    }
    f << backend << ',' << mode << ',' << dataSeed << ',' << stage << ',' << MeanAbs(out, ref) << ',' << MeanSq(out, ref)
      << '\n';
}

Row RunOne(const Runtime& rt, const std::string& mode, double keepProb, bool safeBootstrap, uint32_t dataSeed,
           uint32_t sampleCount, double inputAbsMax, double decisionMargin, double w1, double w2, double w12,
           double bias, uint32_t productPower, bool interactionLabel, double nearBoundaryTau) {
    Row row;
    row.mode = mode;
    row.dataSeed = dataSeed;
    try {
        SetModeEnv(mode, keepProb, safeBootstrap);
        setenv("OPENFHE_CCFA_SEED", std::to_string(dataSeed).c_str(), 1);
        const std::string stageOut = ReadEnvString("OPENFHE_CCFA_STAGE_ALIGN_OUT", "");
        std::mt19937 rng(dataSeed);
        std::uniform_real_distribution<double> dist(-inputAbsMax, inputAbsMax);

        std::vector<double> x1(sampleCount), x2(sampleCount), x3, x4;
        if (productPower >= 2) {
            x3.resize(sampleCount);
        }
        if (productPower >= 3) {
            x4.resize(sampleCount);
        }
        for (uint32_t i = 0; i < sampleCount; ++i) {
            while (true) {
                x1[i] = dist(rng);
                x2[i] = dist(rng);
                if (productPower >= 2) {
                    x3[i] = dist(rng);
                }
                if (productPower >= 3) {
                    x4[i] = dist(rng);
                }
                double prod = x1[i] * x2[i];
                if (productPower >= 2) {
                    prod *= x3[i];
                }
                if (productPower >= 3) {
                    prod *= x4[i];
                }
                if (nearBoundaryTau <= 0.0 || std::abs(prod) <= nearBoundaryTau) {
                    break;
                }
            }
        }
        const auto refLogits = EvalReference(x1, x2, w1, w2, w12, bias, productPower, x3, x4);
        std::vector<double> refInter(sampleCount);
        for (uint32_t i = 0; i < sampleCount; ++i) {
            refInter[i] = x1[i] * x2[i];
            if (productPower >= 2) {
                refInter[i] *= x3[i];
            }
            if (productPower >= 3) {
                refInter[i] *= x4[i];
            }
        }
        std::vector<int> refY(sampleCount);
        for (uint32_t i = 0; i < sampleCount; ++i) {
            if (interactionLabel) {
                double prod = x1[i] * x2[i];
                if (productPower >= 2) {
                    prod *= x3[i];
                }
                if (productPower >= 3) {
                    prod *= x4[i];
                }
                refY[i] = (prod > decisionMargin) ? 1 : 0;
            }
            else {
                refY[i] = (refLogits[i] > decisionMargin) ? 1 : 0;
            }
        }

        auto cc = rt.cc;
        const auto& kp = rt.kp;
        Plaintext pt1 = cc->MakeCKKSPackedPlaintext(x1, 1, rt.depth - 1, nullptr, rt.slots);
        Plaintext pt2 = cc->MakeCKKSPackedPlaintext(x2, 1, rt.depth - 1, nullptr, rt.slots);
        Plaintext pt3;
        Plaintext pt4;
        pt1->SetLength(sampleCount);
        pt2->SetLength(sampleCount);
        auto ct1 = cc->Encrypt(kp.publicKey, pt1);
        auto ct2 = cc->Encrypt(kp.publicKey, pt2);
        ct1->SetSlots(rt.slots);
        ct2->SetSlots(rt.slots);
        Ciphertext<DCRTPoly> ct3;
        Ciphertext<DCRTPoly> ct4;
        if (productPower >= 2) {
            pt3 = cc->MakeCKKSPackedPlaintext(x3, 1, rt.depth - 1, nullptr, rt.slots);
            pt3->SetLength(sampleCount);
            ct3 = cc->Encrypt(kp.publicKey, pt3);
            ct3->SetSlots(rt.slots);
        }
        if (productPower >= 3) {
            pt4 = cc->MakeCKKSPackedPlaintext(x4, 1, rt.depth - 1, nullptr, rt.slots);
            pt4->SetLength(sampleCount);
            ct4 = cc->Encrypt(kp.publicKey, pt4);
            ct4->SetSlots(rt.slots);
        }

        const auto start = std::chrono::high_resolution_clock::now();

        auto h1 = rt.useGPU ? cc->EvalBootstrapGPU(ct1, *rt.gpuContext) : cc->EvalBootstrap(ct1);
        auto h2 = rt.useGPU ? cc->EvalBootstrapGPU(ct2, *rt.gpuContext) : cc->EvalBootstrap(ct2);
        Ciphertext<DCRTPoly> h3;
        Ciphertext<DCRTPoly> h4;
        if (productPower >= 2) {
            h3 = rt.useGPU ? cc->EvalBootstrapGPU(ct3, *rt.gpuContext) : cc->EvalBootstrap(ct3);
        }
        if (productPower >= 3) {
            h4 = rt.useGPU ? cc->EvalBootstrapGPU(ct4, *rt.gpuContext) : cc->EvalBootstrap(ct4);
        }
        auto inter = cc->EvalMult(h1, h2);
        if (productPower >= 2) {
            inter = cc->EvalMult(inter, h3);
        }
        if (productPower >= 3) {
            inter = cc->EvalMult(inter, h4);
        }
        auto logit = cc->EvalAdd(cc->EvalAdd(cc->EvalMult(h1, w1), cc->EvalMult(h2, w2)), cc->EvalMult(inter, w12));
        logit = cc->EvalAdd(logit, bias);

        const auto stop = std::chrono::high_resolution_clock::now();
        row.latencyMs = std::chrono::duration<double, std::milli>(stop - start).count();

        auto out = DecryptReal(cc, kp.secretKey, logit, sampleCount);
        if (!stageOut.empty()) {
            AppendStageRow(stageOut, "gpu", mode, dataSeed, "h1", DecryptReal(cc, kp.secretKey, h1, sampleCount), x1);
            AppendStageRow(stageOut, "gpu", mode, dataSeed, "h2", DecryptReal(cc, kp.secretKey, h2, sampleCount), x2);
            if (productPower >= 2) {
                AppendStageRow(stageOut, "gpu", mode, dataSeed, "h3", DecryptReal(cc, kp.secretKey, h3, sampleCount), x3);
            }
            if (productPower >= 3) {
                AppendStageRow(stageOut, "gpu", mode, dataSeed, "h4", DecryptReal(cc, kp.secretKey, h4, sampleCount), x4);
            }
            AppendStageRow(stageOut, "gpu", mode, dataSeed, "inter", DecryptReal(cc, kp.secretKey, inter, sampleCount),
                           refInter);
            AppendStageRow(stageOut, "gpu", mode, dataSeed, "logit", out, refLogits);
        }
        row.finite = true;
        for (double v : out) {
            if (!std::isfinite(v)) {
                row.finite = false;
                break;
            }
        }
        row.success = row.finite;
        if (row.finite) {
            row.logitMae = MeanAbs(out, refLogits);
            row.logitMse = MeanSq(out, refLogits);
            uint32_t correct = 0;
            for (uint32_t i = 0; i < sampleCount; ++i) {
                int pred;
                if (interactionLabel) {
                    pred = (out[i] > decisionMargin) ? 1 : 0;
                }
                else {
                    pred = (out[i] > decisionMargin) ? 1 : 0;
                }
                if (pred == refY[i]) {
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
    const uint32_t seedEnd = ReadEnvUInt("OPENFHE_CCFA_DATA_SEED_END", 10);
    const uint32_t sampleCount = ReadEnvUInt("OPENFHE_CCFA_SAMPLE_COUNT", 64);
    const double inputAbsMax = ReadEnvDouble("OPENFHE_CCFA_INPUT_ABS_MAX", 1.0);
    const double decisionMargin = ReadEnvDouble("OPENFHE_CCFA_DECISION_MARGIN", 0.0);
    const double w1 = ReadEnvDouble("OPENFHE_CCFA_LOGREG_W1", 0.8);
    const double w2 = ReadEnvDouble("OPENFHE_CCFA_LOGREG_W2", -0.55);
    const double w12 = ReadEnvDouble("OPENFHE_CCFA_LOGREG_W12", 1.25);
    const double bias = ReadEnvDouble("OPENFHE_CCFA_LOGREG_BIAS", -0.05);
    const uint32_t productPower = ReadEnvUInt("OPENFHE_CCFA_PRODUCT_POWER", 1);
    const bool interactionLabel = ReadEnvBool("OPENFHE_CCFA_INTERACTION_LABEL", false);
    const double nearBoundaryTau = ReadEnvDouble("OPENFHE_CCFA_NEAR_BOUNDARY_TAU", 0.0);
    const std::string outCsv =
        ReadEnvString("OPENFHE_CCFA_HE_LOGREG_OUTPUT", "/workspace/results/openfhe_ccfa_he_logreg_bench.csv");
    const std::string singleMode = ReadEnvString("OPENFHE_CCFA_HE_LOGREG_SINGLE", "");

    std::filesystem::create_directories(std::filesystem::path(outCsv).parent_path());
    std::cerr << "[he-mlp] setup\n";
    auto rt = SetupRuntime(ringDim, dcrtBits, firstMod, levelsAfter, slotsOverride, {levelBudget0, levelBudget1});
    std::cerr << "[he-mlp] setup-done\n";

    std::ofstream out(outCsv);
    out << "mode,data_seed,success,finite,latency_ms,logit_mae,logit_mse,accuracy,error\n";
    out << std::scientific << std::setprecision(12);
    for (uint32_t seed = seedStart; seed <= seedEnd; ++seed) {
        for (const auto& cfg : std::vector<std::tuple<std::string, double, bool>>{
                 {"deterministic", 1.0, false}, {"independent", keepProb, false}, {"product_safe", keepProb, true}}) {
            if (!singleMode.empty() && singleMode != std::get<0>(cfg)) {
                continue;
            }
            std::cerr << "[he-logreg] seed=" << seed << " mode=" << std::get<0>(cfg) << "\n";
            Row row = RunOne(rt, std::get<0>(cfg), std::get<1>(cfg), std::get<2>(cfg), seed, sampleCount, inputAbsMax,
                             decisionMargin, w1, w2, w12, bias, productPower, interactionLabel, nearBoundaryTau);
            out << row.mode << ',' << row.dataSeed << ',' << (row.success ? 1 : 0) << ',' << (row.finite ? 1 : 0) << ','
                << row.latencyMs << ',' << row.logitMae << ',' << row.logitMse << ',' << row.accuracy << ",\""
                << row.error << "\"\n";
            out.flush();
            std::cerr << "[he-mlp] seed=" << seed << " mode=" << std::get<0>(cfg) << " done\n";
        }
    }
    std::cout << "Wrote results to " << outCsv << std::endl;
    return 0;
}
