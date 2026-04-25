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
#include <random>
#include <string>
#include <vector>

using namespace lbcrypto;

namespace {

uint32_t ReadEnvUInt(const char* name, uint32_t fallback) {
    const char* value = std::getenv(name);
    return value ? static_cast<uint32_t>(std::strtoul(value, nullptr, 10)) : fallback;
}

std::string ReadEnvString(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : fallback;
}

double ReadEnvDouble(const char* name, double fallback) {
    const char* value = std::getenv(name);
    return value ? std::stod(value) : fallback;
}

void SetModeEnv(const std::string& mode, double keepProb, bool safeBootstrap) {
    setenv("OPENFHE_CCFA_MODE", mode.c_str(), 1);
    setenv("OPENFHE_CCFA_KEEP_PROB", std::to_string(keepProb).c_str(), 1);
    if (const char* seed = std::getenv("OPENFHE_CCFA_BOOT_SEED")) {
        setenv("OPENFHE_CCFA_SEED", seed, 1);
    }
    else {
        setenv("OPENFHE_CCFA_SEED", "1", 1);
    }
    setenv("OPENFHE_CCFA_MIN_M", "2", 1);
    setenv("OPENFHE_CCFA_MAX_M", "16", 1);
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
    unsetenv("OPENFHE_CCFA_SEED");
    unsetenv("OPENFHE_CCFA_MIN_M");
    unsetenv("OPENFHE_CCFA_MAX_M");
    unsetenv("OPENFHE_CCFA_SAFE_BOOTSTRAP");
    unsetenv("OPENFHE_CCFA_SAFE_MAX_M");
    unsetenv("OPENFHE_CCFA_SAFE_DISABLE_CU");
    unsetenv("OPENFHE_CCFA_SAFE_DETERMINISTIC_MIN_M");
}

double MeanAbsError(const std::vector<double>& a, const std::vector<double>& b) {
    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        sum += std::abs(a[i] - b[i]);
    }
    return sum / static_cast<double>(a.size());
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

struct BenchRow {
    std::string label;
    uint32_t inputSeed = 0;
    bool success = false;
    bool finite = false;
    double latencyMs = 0.0;
    double outputErrorX = std::numeric_limits<double>::quiet_NaN();
    double outputErrorY = std::numeric_limits<double>::quiet_NaN();
    double productError = std::numeric_limits<double>::quiet_NaN();
    double crossError = std::numeric_limits<double>::quiet_NaN();
    double repeatedProductError = std::numeric_limits<double>::quiet_NaN();
    std::string error;
};

struct BenchRuntime {
    CryptoContext<DCRTPoly> cc;
    KeyPair<DCRTPoly> kp;
    uint32_t depth = 0;
    uint32_t numSlots = 0;
    uint32_t correctionFactor = 0;
};

BenchRuntime SetupRuntime(uint32_t ringDim, uint32_t dcrtBits, uint32_t firstMod, uint32_t levelsAfter,
                          uint32_t correctionFactor) {
    BenchRuntime rt;
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

    rt.numSlots = rt.cc->GetRingDimension() / 2;
    rt.cc->EvalBootstrapSetup(levelBudget, {0, 0}, rt.numSlots, correctionFactor);
    rt.kp = rt.cc->KeyGen();
    rt.cc->EvalMultKeyGen(rt.kp.secretKey);
    rt.cc->EvalBootstrapKeyGen(rt.kp.secretKey, rt.numSlots);
    rt.correctionFactor = correctionFactor;
    return rt;
}

BenchRow RunOne(const std::string& label, const std::string& mode, double keepProb, bool safeBootstrap,
                const BenchRuntime& rt, uint32_t inputSeed,
                double inputAbsMax, uint32_t productPower) {
    BenchRow row;
    row.label = label;
    row.inputSeed = inputSeed;
    try {
        SetModeEnv(mode, keepProb, safeBootstrap);
        auto cc = rt.cc;
        const auto& kp = rt.kp;
        const uint32_t depth = rt.depth;

        std::mt19937 rng(inputSeed);
        std::uniform_real_distribution<double> dist(-inputAbsMax, inputAbsMax);
        std::vector<double> x(8), y(8);
        for (size_t i = 0; i < x.size(); ++i) {
            x[i] = dist(rng);
            y[i] = dist(rng);
        }

        Plaintext ptx = cc->MakeCKKSPackedPlaintext(x, 1, depth - 1);
        ptx->SetLength(x.size());
        Plaintext pty = cc->MakeCKKSPackedPlaintext(y, 1, depth - 1);
        pty->SetLength(y.size());
        auto ctx = cc->Encrypt(kp.publicKey, ptx);
        auto cty = cc->Encrypt(kp.publicKey, pty);

        const auto start = std::chrono::high_resolution_clock::now();
        auto outx = cc->EvalBootstrap(ctx);
        auto outy = cc->EvalBootstrap(cty);
        const auto stop = std::chrono::high_resolution_clock::now();
        row.latencyMs = std::chrono::duration<double, std::milli>(stop - start).count();

        auto dx = DecryptReal(cc, kp.secretKey, outx, x.size());
        auto dy = DecryptReal(cc, kp.secretKey, outy, y.size());

        row.finite = true;
        for (size_t i = 0; i < x.size(); ++i) {
            if (!std::isfinite(dx[i]) || !std::isfinite(dy[i])) {
                row.finite = false;
                break;
            }
        }
        row.success = row.finite;
        if (row.finite) {
            row.outputErrorX = MeanAbsError(dx, x);
            row.outputErrorY = MeanAbsError(dy, y);
            std::vector<double> prodRef(x.size()), prodOut(x.size()), cross(x.size());
            for (size_t i = 0; i < x.size(); ++i) {
                prodRef[i] = x[i] * y[i];
                prodOut[i] = dx[i] * dy[i];
                cross[i]   = (dx[i] - x[i]) * (dy[i] - y[i]);
            }
            row.productError = MeanAbsError(prodOut, prodRef);
            std::vector<double> zero(x.size(), 0.0);
            row.crossError = MeanAbsError(cross, zero);
            if (productPower > 1) {
                std::vector<double> repRef(x.size()), repOut(x.size());
                for (size_t i = 0; i < x.size(); ++i) {
                    double ref = x[i];
                    double out = dx[i];
                    for (uint32_t p = 1; p < productPower; ++p) {
                        ref *= y[i];
                        out *= dy[i];
                    }
                    repRef[i] = ref;
                    repOut[i] = out;
                }
                row.repeatedProductError = MeanAbsError(repOut, repRef);
            }
            else {
                row.repeatedProductError = row.productError;
            }
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
    const double keepProb = ReadEnvDouble("OPENFHE_CCFA_BOOT_KEEP_PROB", 0.1);
    const uint32_t inputSeed = ReadEnvUInt("OPENFHE_CCFA_INPUT_SEED", 17);
    const uint32_t inputSeedStart = ReadEnvUInt("OPENFHE_CCFA_INPUT_SEED_START", inputSeed);
    const uint32_t inputSeedEnd = ReadEnvUInt("OPENFHE_CCFA_INPUT_SEED_END", inputSeed);
    const double inputAbsMax = ReadEnvDouble("OPENFHE_CCFA_INPUT_ABS_MAX", 0.25);
    const uint32_t productPower = ReadEnvUInt("OPENFHE_CCFA_PRODUCT_POWER", 1);
    const uint32_t correctionFactor = ReadEnvUInt("OPENFHE_CCFA_BOOT_CORRECTION_FACTOR", 0);
    const std::string outCsv =
        ReadEnvString("OPENFHE_CCFA_BOOT_OUTPUT", "/workspace/results/openfhe_ccfa_full_bootstrap_structure.csv");
    const std::string singleLabel = ReadEnvString("OPENFHE_CCFA_BOOT_SINGLE", "");

    std::filesystem::create_directories(std::filesystem::path(outCsv).parent_path());

    const bool runDefault = singleLabel.empty() || singleLabel == "default";
    const bool runProduct = singleLabel.empty() || singleLabel == "product_safe";

    BenchRuntime rtDefault;
    BenchRuntime rtProduct;
    if (runDefault) {
        rtDefault = SetupRuntime(ringDim, dcrtBits, firstMod, levelsAfter, correctionFactor);
    }
    if (runProduct) {
        rtProduct = SetupRuntime(ringDim, dcrtBits, firstMod, levelsAfter, correctionFactor);
    }

    std::vector<BenchRow> rows;
    auto maybeRun = [&](const std::string& label, const std::string& mode, double keep, bool safe, const BenchRuntime& rt,
                        uint32_t seed) {
        rows.push_back(RunOne(label, mode, keep, safe, rt, seed, inputAbsMax, productPower));
    };

    for (uint32_t seed = inputSeedStart; seed <= inputSeedEnd; ++seed) {
        if (runDefault) {
            maybeRun("default", "none", 1.0, false, rtDefault, seed);
        }
        if (runProduct) {
            maybeRun("product_safe", "product", keepProb, true, rtProduct, seed);
        }
    }

    std::ofstream out(outCsv);
    out << "label,success,finite,latency_ms,output_error_x,output_error_y,product_error,cross_error,repeated_product_error,error,ring_dim,dcrt_bits,first_mod,levels_after,correction_factor,input_seed,input_abs_max,product_power\n";
    out << std::scientific << std::setprecision(12);
    for (const auto& row : rows) {
        out << row.label << ',' << (row.success ? 1 : 0) << ',' << (row.finite ? 1 : 0) << ',' << row.latencyMs
            << ',' << row.outputErrorX << ',' << row.outputErrorY << ',' << row.productError << ',' << row.crossError
            << ',' << row.repeatedProductError << ",\"" << row.error << "\"," << ringDim << ',' << dcrtBits << ','
            << firstMod << ',' << levelsAfter << ',' << correctionFactor << ',' << row.inputSeed << ',' << inputAbsMax << ','
            << productPower << '\n';
    }
    std::cout << "Wrote results to " << outCsv << std::endl;
    return 0;
}
