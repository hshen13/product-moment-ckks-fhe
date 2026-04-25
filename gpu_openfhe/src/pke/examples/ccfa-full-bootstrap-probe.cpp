//==================================================================================
// BSD 2-Clause License
//==================================================================================

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
#include <string>
#include <vector>

using namespace lbcrypto;

namespace {

struct ProbeRow {
    std::string label;
    bool success;
    bool finite;
    double latencyMs;
    double maeVsInput;
    std::string error;
};

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

ProbeRow RunOne(const std::string& label, const std::string& mode, double keepProb, bool safeBootstrap,
                uint32_t ringDim, uint32_t dcrtBits, uint32_t firstMod, uint32_t levelsAfter) {
    ProbeRow row{label, false, false, 0.0, std::numeric_limits<double>::quiet_NaN(), ""};
    try {
        SetModeEnv(mode, keepProb, safeBootstrap);

        CCParams<CryptoContextCKKSRNS> parameters;
        SecretKeyDist secretKeyDist = UNIFORM_TERNARY;
        parameters.SetSecretKeyDist(secretKeyDist);
        parameters.SetSecurityLevel(HEStd_NotSet);
        parameters.SetRingDim(ringDim);
        parameters.SetScalingTechnique(FLEXIBLEAUTO);
        parameters.SetScalingModSize(dcrtBits);
        parameters.SetFirstModSize(firstMod);
        std::vector<uint32_t> levelBudget = {4, 4};
        uint32_t depth = levelsAfter + FHECKKSRNS::GetBootstrapDepth(levelBudget, secretKeyDist);
        parameters.SetMultiplicativeDepth(depth);

        CryptoContext<DCRTPoly> cc = GenCryptoContext(parameters);
        cc->Enable(PKE);
        cc->Enable(KEYSWITCH);
        cc->Enable(LEVELEDSHE);
        cc->Enable(ADVANCEDSHE);
        cc->Enable(FHE);

        uint32_t numSlots = cc->GetRingDimension() / 2;
        cc->EvalBootstrapSetup(levelBudget, {0, 0}, numSlots);
        auto kp = cc->KeyGen();
        cc->EvalMultKeyGen(kp.secretKey);
        cc->EvalBootstrapKeyGen(kp.secretKey, numSlots);

        std::vector<double> input = {0.25, 0.5, 0.75, 1.0, 2.0, 3.0, 4.0, 5.0};
        Plaintext pt = cc->MakeCKKSPackedPlaintext(input, 1, depth - 1);
        pt->SetLength(input.size());
        auto ct = cc->Encrypt(kp.publicKey, pt);

        const auto start = std::chrono::high_resolution_clock::now();
        auto out = cc->EvalBootstrap(ct);
        const auto stop = std::chrono::high_resolution_clock::now();
        row.latencyMs = std::chrono::duration<double, std::milli>(stop - start).count();

        Plaintext dec;
        cc->Decrypt(kp.secretKey, out, &dec);
        dec->SetLength(input.size());
        std::vector<double> values;
        row.finite = true;
        for (const auto& v : dec->GetCKKSPackedValue()) {
            const double real = v.real();
            values.push_back(real);
            if (!std::isfinite(real)) {
                row.finite = false;
            }
        }
        row.maeVsInput = row.finite ? MeanAbsError(values, input) : std::numeric_limits<double>::quiet_NaN();
        row.success    = row.finite;
        cc->ClearStaticMapsAndVectors();
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
    const uint32_t ringDim    = ReadEnvUInt("OPENFHE_CCFA_BOOT_RING_DIM", 1u << 12);
    const uint32_t dcrtBits   = ReadEnvUInt("OPENFHE_CCFA_BOOT_DCRT_BITS", 59);
    const uint32_t firstMod   = ReadEnvUInt("OPENFHE_CCFA_BOOT_FIRST_MOD", 60);
    const uint32_t levelsAfter = ReadEnvUInt("OPENFHE_CCFA_BOOT_LEVELS_AFTER", 10);
    const double keepProb     = ReadEnvDouble("OPENFHE_CCFA_BOOT_KEEP_PROB", 0.1);
    const std::string outCsv  = ReadEnvString("OPENFHE_CCFA_BOOT_OUTPUT",
                                             "/workspace/results/openfhe_ccfa_full_bootstrap_probe.csv");
    const std::string singleLabel = ReadEnvString("OPENFHE_CCFA_BOOT_SINGLE", "");

    std::filesystem::create_directories(std::filesystem::path(outCsv).parent_path());

    std::vector<ProbeRow> rows;
    auto maybeRun = [&](const std::string& label, const std::string& mode, double keep, bool safe) {
        if (!singleLabel.empty() && singleLabel != label) {
            return;
        }
        rows.push_back(RunOne(label, mode, keep, safe, ringDim, dcrtBits, firstMod, levelsAfter));
    };

    maybeRun("default", "none", 1.0, false);
    maybeRun("independent", "independent", keepProb, false);
    maybeRun("product", "product", keepProb, false);
    maybeRun("product_safe", "product", keepProb, true);

    std::ofstream out(outCsv);
    out << "label,success,finite,latency_ms,mae_vs_input,error,ring_dim,dcrt_bits,first_mod,levels_after\n";
    for (const auto& row : rows) {
        out << row.label << ',' << (row.success ? 1 : 0) << ',' << (row.finite ? 1 : 0) << ',' << std::fixed
            << std::setprecision(6) << row.latencyMs << ',' << row.maeVsInput << ",\"" << row.error << "\"," << ringDim
            << ',' << dcrtBits << ',' << firstMod << ',' << levelsAfter << '\n';
    }

    std::cout << "Wrote results to " << outCsv << std::endl;
    return 0;
}
