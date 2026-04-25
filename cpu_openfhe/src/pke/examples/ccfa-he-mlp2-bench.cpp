#include "openfhe.h"

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
};

Runtime SetupRuntime(uint32_t ringDim, uint32_t dcrtBits, uint32_t firstMod, uint32_t levelsAfter) {
    Runtime rt;
    CCParams<CryptoContextCKKSRNS> parameters;
    SecretKeyDist secretKeyDist = UNIFORM_TERNARY;
    parameters.SetSecretKeyDist(secretKeyDist);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(ringDim);
    parameters.SetScalingTechnique(FLEXIBLEAUTO);
    parameters.SetScalingModSize(dcrtBits);
    parameters.SetFirstModSize(firstMod);
    std::vector<uint32_t> levelBudget = {4, 4};
    rt.depth = levelsAfter + FHECKKSRNS::GetBootstrapDepth(levelBudget, secretKeyDist);
    parameters.SetMultiplicativeDepth(rt.depth);

    rt.cc = GenCryptoContext(parameters);
    rt.cc->Enable(PKE);
    rt.cc->Enable(KEYSWITCH);
    rt.cc->Enable(LEVELEDSHE);
    rt.cc->Enable(ADVANCEDSHE);
    rt.cc->Enable(FHE);

    rt.slots = rt.cc->GetRingDimension() / 2;
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

struct ReferenceBatch {
    std::vector<double> h1;
    std::vector<double> h2;
    std::vector<double> logit;
    std::vector<int> y;
};

ReferenceBatch MakeReference(const std::vector<double>& x1, const std::vector<double>& x2, const std::vector<double>& x3,
                             const std::vector<double>& x4, double wh1, double wh2, double wout, double bias,
                             double decisionMargin) {
    ReferenceBatch ref;
    const size_t n = x1.size();
    ref.h1.resize(n);
    ref.h2.resize(n);
    ref.logit.resize(n);
    ref.y.resize(n);
    for (size_t i = 0; i < n; ++i) {
        ref.h1[i] = x1[i] * x2[i];
        ref.h2[i] = x3[i] * x4[i];
        ref.logit[i] = wh1 * ref.h1[i] + wh2 * ref.h2[i] + wout * (ref.h1[i] * ref.h2[i]) + bias;
        ref.y[i] = (ref.logit[i] > decisionMargin) ? 1 : 0;
    }
    return ref;
}

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
           uint32_t sampleCount, double inputAbsMax, double wh1, double wh2, double wout, double bias,
           bool refreshHidden, bool compactMode, double hiddenMix, double decisionMargin) {
    Row row;
    row.mode = mode;
    row.dataSeed = dataSeed;
    try {
        SetModeEnv(mode, keepProb, safeBootstrap);
        setenv("OPENFHE_CCFA_SEED", std::to_string(dataSeed).c_str(), 1);

        std::mt19937 rng(dataSeed);
        std::uniform_real_distribution<double> dist(-inputAbsMax, inputAbsMax);
        std::vector<double> x1(sampleCount), x2(sampleCount), x3(sampleCount), x4(sampleCount);
        for (uint32_t i = 0; i < sampleCount; ++i) {
            x1[i] = dist(rng);
            x2[i] = dist(rng);
            if (!compactMode) {
                x3[i] = dist(rng);
                x4[i] = dist(rng);
            }
        }
        const auto ref = compactMode ? MakeReferenceCompact(x1, x2, hiddenMix, wh1, wh2, wout, bias, decisionMargin)
                                     : MakeReference(x1, x2, x3, x4, wh1, wh2, wout, bias, decisionMargin);

        auto cc = rt.cc;
        const auto& kp = rt.kp;
        Plaintext pt1 = cc->MakeCKKSPackedPlaintext(x1, 1, rt.depth - 1);
        Plaintext pt2 = cc->MakeCKKSPackedPlaintext(x2, 1, rt.depth - 1);
        pt1->SetLength(sampleCount);
        pt2->SetLength(sampleCount);

        auto ct1 = cc->Encrypt(kp.publicKey, pt1);
        auto ct2 = cc->Encrypt(kp.publicKey, pt2);
        Ciphertext<DCRTPoly> ct3;
        Ciphertext<DCRTPoly> ct4;
        if (!compactMode) {
            Plaintext pt3 = cc->MakeCKKSPackedPlaintext(x3, 1, rt.depth - 1);
            Plaintext pt4 = cc->MakeCKKSPackedPlaintext(x4, 1, rt.depth - 1);
            pt3->SetLength(sampleCount);
            pt4->SetLength(sampleCount);
            ct3 = cc->Encrypt(kp.publicKey, pt3);
            ct4 = cc->Encrypt(kp.publicKey, pt4);
        }

        const auto start = std::chrono::high_resolution_clock::now();

        auto b1 = cc->EvalBootstrap(ct1);
        auto b2 = cc->EvalBootstrap(ct2);
        auto h1 = cc->EvalMult(b1, b2);
        Ciphertext<DCRTPoly> h2;
        if (compactMode) {
            auto lin = cc->EvalAdd(cc->EvalMult(b1, hiddenMix), cc->EvalMult(b2, 1.0 - hiddenMix));
            h2 = cc->EvalMult(lin, lin);
        }
        else {
            auto b3 = cc->EvalBootstrap(ct3);
            auto b4 = cc->EvalBootstrap(ct4);
            h2 = cc->EvalMult(b3, b4);
        }

        Ciphertext<DCRTPoly> hb1 = h1;
        Ciphertext<DCRTPoly> hb2 = h2;
        if (refreshHidden) {
            hb1 = cc->EvalBootstrap(h1);
            hb2 = cc->EvalBootstrap(h2);
        }

        auto out = cc->EvalAdd(cc->EvalAdd(cc->EvalMult(hb1, wh1), cc->EvalMult(hb2, wh2)), cc->EvalMult(cc->EvalMult(hb1, hb2), wout));
        out = cc->EvalAdd(out, bias);

        const auto stop = std::chrono::high_resolution_clock::now();
        row.latencyMs = std::chrono::duration<double, std::milli>(stop - start).count();

        auto h1Out = DecryptReal(cc, kp.secretKey, hb1, sampleCount);
        auto h2Out = DecryptReal(cc, kp.secretKey, hb2, sampleCount);
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
    const uint32_t ringDim = ReadEnvUInt("OPENFHE_CCFA_BOOT_RING_DIM", 1u << 11);
    const uint32_t dcrtBits = ReadEnvUInt("OPENFHE_CCFA_BOOT_DCRT_BITS", 59);
    const uint32_t firstMod = ReadEnvUInt("OPENFHE_CCFA_BOOT_FIRST_MOD", 60);
    const uint32_t levelsAfter = ReadEnvUInt("OPENFHE_CCFA_BOOT_LEVELS_AFTER", 10);
    const double keepProb = ReadEnvDouble("OPENFHE_CCFA_BOOT_KEEP_PROB", 0.6);
    const uint32_t seedStart = ReadEnvUInt("OPENFHE_CCFA_DATA_SEED_START", 1);
    const uint32_t seedEnd = ReadEnvUInt("OPENFHE_CCFA_DATA_SEED_END", 3);
    const uint32_t sampleCount = ReadEnvUInt("OPENFHE_CCFA_SAMPLE_COUNT", 8);
    const double inputAbsMax = ReadEnvDouble("OPENFHE_CCFA_INPUT_ABS_MAX", 0.5);
    const double decisionMargin = ReadEnvDouble("OPENFHE_CCFA_DECISION_MARGIN", 0.0);
    const double wh1 = ReadEnvDouble("OPENFHE_CCFA_MLP2_WH1", 0.7);
    const double wh2 = ReadEnvDouble("OPENFHE_CCFA_MLP2_WH2", -0.4);
    const double wout = ReadEnvDouble("OPENFHE_CCFA_MLP2_WOUT", 1.5);
    const double bias = ReadEnvDouble("OPENFHE_CCFA_MLP2_BIAS", -0.05);
    const bool refreshHidden = ReadEnvUInt("OPENFHE_CCFA_MLP2_REFRESH_HIDDEN", 0) != 0;
    const bool compactMode = ReadEnvUInt("OPENFHE_CCFA_MLP2_COMPACT", 1) != 0;
    const double hiddenMix = ReadEnvDouble("OPENFHE_CCFA_MLP2_HIDDEN_MIX", 0.65);
    const std::string outCsv =
        ReadEnvString("OPENFHE_CCFA_HE_MLP2_OUTPUT", "/workspace/results/openfhe_ccfa_he_mlp2_bench.csv");
    const std::string singleMode = ReadEnvString("OPENFHE_CCFA_HE_MLP2_SINGLE", "");

    std::filesystem::create_directories(std::filesystem::path(outCsv).parent_path());
    auto rt = SetupRuntime(ringDim, dcrtBits, firstMod, levelsAfter);

    std::ofstream out(outCsv);
    out << "mode,data_seed,success,finite,latency_ms,h1_mae,h2_mae,logit_mae,logit_mse,accuracy,error\n";
    out << std::scientific << std::setprecision(12);
    for (uint32_t seed = seedStart; seed <= seedEnd; ++seed) {
        for (const auto& cfg : std::vector<std::tuple<std::string, double, bool>>{
                 {"deterministic", 1.0, false}, {"independent", keepProb, false}, {"product_safe", keepProb, true}}) {
            if (!singleMode.empty() && singleMode != std::get<0>(cfg)) {
                continue;
            }
            Row row = RunOne(rt, std::get<0>(cfg), std::get<1>(cfg), std::get<2>(cfg), seed, sampleCount, inputAbsMax,
                             wh1, wh2, wout, bias, refreshHidden, compactMode, hiddenMix, decisionMargin);
            out << row.mode << ',' << row.dataSeed << ',' << (row.success ? 1 : 0) << ',' << (row.finite ? 1 : 0) << ','
                << row.latencyMs << ',' << row.h1Mae << ',' << row.h2Mae << ',' << row.logitMae << ',' << row.logitMse
                << ',' << row.accuracy << ",\"" << row.error << "\"\n";
            out.flush();
        }
    }
    std::cout << "Wrote results to " << outCsv << std::endl;
    return 0;
}
