//==================================================================================
// BSD 2-Clause License
//
// Copyright (c) 2014-2022, NJIT, Duality Technologies Inc. and other contributors
//
// All rights reserved.
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
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace lbcrypto;

namespace {

struct ExperimentConfig {
    uint32_t ringDim;
    uint32_t dcrtBits;
    uint32_t firstMod;
    uint32_t multDepth;
    double a;
    double b;
    std::vector<double> keepProbs;
    std::vector<uint64_t> seeds;
    std::string outputCsv;
};

struct TrialMetrics {
    std::string mode;
    double keepProb;
    uint64_t seed;
    bool success;
    double latencyMs;
    double outputError;
    double productError;
    double crossError;
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

std::vector<double> ParseDoubleList(const std::string& text) {
    std::vector<double> values;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            values.push_back(std::stod(item));
        }
    }
    return values;
}

int ReadEnvInt(const char* name, int fallback) {
    const char* value = std::getenv(name);
    return value ? static_cast<int>(std::strtol(value, nullptr, 10)) : fallback;
}

std::vector<uint64_t> ParseUInt64List(const std::string& text) {
    std::vector<uint64_t> values;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            values.push_back(static_cast<uint64_t>(std::strtoull(item.c_str(), nullptr, 10)));
        }
    }
    return values;
}

ExperimentConfig ReadConfig() {
    ExperimentConfig cfg;
    cfg.ringDim   = ReadEnvUInt("OPENFHE_CCFA_RING_DIM", 1u << 12);
    cfg.dcrtBits  = ReadEnvUInt("OPENFHE_CCFA_DCRT_BITS", 50);
    cfg.firstMod  = ReadEnvUInt("OPENFHE_CCFA_FIRST_MOD", 60);
    cfg.multDepth = ReadEnvUInt("OPENFHE_CCFA_MULT_DEPTH", 7);
    cfg.a         = -4.0;
    cfg.b         = 4.0;
    cfg.keepProbs = ParseDoubleList(ReadEnvString("OPENFHE_CCFA_KEEP_LIST", "0.5,0.25,0.1"));
    cfg.seeds     = ParseUInt64List(ReadEnvString("OPENFHE_CCFA_SEED_LIST", "1,2,3"));
    cfg.outputCsv = ReadEnvString("OPENFHE_CCFA_OUTPUT", "/workspace/results/openfhe_ccfa_kernel_results.csv");
    return cfg;
}

std::vector<double> GetChebyshevCoeffs() {
    return {1.0, 0.558971, 0.0, -0.0943712, 0.0, 0.0215023, 0.0, -0.00505348, 0.0,
            0.00119324, 0.0, -0.000281928, 0.0, 0.0000664347, 0.0, -0.0000148709};
}

std::vector<double> GetSyntheticCoeffs(int degree) {
    std::vector<double> coeffs(static_cast<size_t>(degree) + 1, 0.0);
    coeffs[0] = 0.25;
    for (int i = 1; i <= degree; ++i) {
        if (i % 2 == 1) {
            const double sign = ((i / 2) % 2 == 0) ? 1.0 : -1.0;
            coeffs[static_cast<size_t>(i)] = sign / static_cast<double>((i + 1) * (i + 1));
        }
    }
    return coeffs;
}

std::vector<std::complex<double>> ToComplex(const std::vector<double>& values) {
    std::vector<std::complex<double>> result;
    result.reserve(values.size());
    for (double value : values) {
        result.emplace_back(value, 0.0);
    }
    return result;
}

std::vector<double> DecryptReal(CryptoContext<DCRTPoly> cc, const PrivateKey<DCRTPoly>& sk,
                                ConstCiphertext<DCRTPoly> ct, size_t length) {
    Plaintext pt;
    cc->Decrypt(sk, ct, &pt);
    pt->SetLength(length);
    std::vector<double> out;
    for (const auto& value : pt->GetCKKSPackedValue()) {
        out.push_back(value.real());
    }
    return out;
}

double MeanAbsDiff(const std::vector<double>& lhs, const std::vector<double>& rhs) {
    double sum = 0.0;
    for (size_t i = 0; i < lhs.size(); ++i) {
        sum += std::abs(lhs[i] - rhs[i]);
    }
    return sum / static_cast<double>(lhs.size());
}

double MeanAbsProductDiff(const std::vector<double>& ax, const std::vector<double>& ay,
                          const std::vector<double>& bx, const std::vector<double>& by) {
    double sum = 0.0;
    for (size_t i = 0; i < ax.size(); ++i) {
        sum += std::abs(ax[i] * ay[i] - bx[i] * by[i]);
    }
    return sum / static_cast<double>(ax.size());
}

double MeanAbsCrossError(const std::vector<double>& ax, const std::vector<double>& ay,
                         const std::vector<double>& bx, const std::vector<double>& by) {
    double sum = 0.0;
    for (size_t i = 0; i < ax.size(); ++i) {
        sum += std::abs((ax[i] - bx[i]) * (ay[i] - by[i]));
    }
    return sum / static_cast<double>(ax.size());
}

std::vector<double> EvalChebyshevPlain(const std::vector<double>& coeffs, const std::vector<double>& xs) {
    std::vector<double> out(xs.size(), 0.0);
    for (size_t i = 0; i < xs.size(); ++i) {
        const double x = xs[i];
        if (coeffs.empty()) {
            out[i] = 0.0;
            continue;
        }
        double t0  = 1.0;
        double acc = coeffs[0] * t0;
        if (coeffs.size() == 1) {
            out[i] = acc;
            continue;
        }
        double t1 = x;
        acc += coeffs[1] * t1;
        for (size_t n = 2; n < coeffs.size(); ++n) {
            double tn = 2.0 * x * t1 - t0;
            acc += coeffs[n] * tn;
            t0 = t1;
            t1 = tn;
        }
        out[i] = acc;
    }
    return out;
}

void SetCCFAEnv(const std::string& mode, double keepProb, uint64_t seed) {
    setenv("OPENFHE_CCFA_MODE", mode.c_str(), 1);
    setenv("OPENFHE_CCFA_KEEP_PROB", std::to_string(keepProb).c_str(), 1);
    setenv("OPENFHE_CCFA_SEED", std::to_string(seed).c_str(), 1);
    setenv("OPENFHE_CCFA_MIN_M", "2", 1);
    setenv("OPENFHE_CCFA_MAX_M", "16", 1);
}

void ClearCCFAEnv() {
    setenv("OPENFHE_CCFA_MODE", "none", 1);
    unsetenv("OPENFHE_CCFA_KEEP_PROB");
    unsetenv("OPENFHE_CCFA_SEED");
    unsetenv("OPENFHE_CCFA_MIN_M");
    unsetenv("OPENFHE_CCFA_MAX_M");
}

TrialMetrics RunTrial(CryptoContext<DCRTPoly> cc, const PrivateKey<DCRTPoly>& sk,
                      std::shared_ptr<seriesPowers<DCRTPoly>> xPowers,
                      std::shared_ptr<seriesPowers<DCRTPoly>> yPowers, const std::vector<double>& coeffs,
                      const std::vector<double>& baselineX, const std::vector<double>& baselineY,
                      const std::string& mode, double keepProb, uint64_t seed) {
    TrialMetrics row{mode, keepProb, seed, false, 0.0, 0.0, 0.0, 0.0, ""};
    try {
        SetCCFAEnv(mode, keepProb, seed);
        const auto start = std::chrono::high_resolution_clock::now();
        auto outX        = cc->EvalChebyshevSeriesWithPrecomp(xPowers, coeffs);
        auto outY        = cc->EvalChebyshevSeriesWithPrecomp(yPowers, coeffs);
        const auto stop  = std::chrono::high_resolution_clock::now();
        ClearCCFAEnv();

        const auto decX = DecryptReal(cc, sk, outX, baselineX.size());
        const auto decY = DecryptReal(cc, sk, outY, baselineY.size());
        row.success     = true;
        row.latencyMs =
            std::chrono::duration<double, std::milli>(stop - start).count();
        row.outputError = 0.5 * (MeanAbsDiff(decX, baselineX) + MeanAbsDiff(decY, baselineY));
        row.productError =
            MeanAbsProductDiff(decX, decY, baselineX, baselineY);
        row.crossError = MeanAbsCrossError(decX, decY, baselineX, baselineY);
    }
    catch (const std::exception& e) {
        ClearCCFAEnv();
        row.error = e.what();
    }
    return row;
}

}  // namespace

int main() {
    const auto cfg = ReadConfig();
    std::filesystem::create_directories(std::filesystem::path(cfg.outputCsv).parent_path());

    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(cfg.ringDim);
    parameters.SetScalingTechnique(FLEXIBLEAUTO);
    parameters.SetScalingModSize(cfg.dcrtBits);
    parameters.SetFirstModSize(cfg.firstMod);
    parameters.SetMultiplicativeDepth(cfg.multDepth);

    auto cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    cc->Enable(ADVANCEDSHE);

    auto kp = cc->KeyGen();
    cc->EvalMultKeyGen(kp.secretKey);

    const std::vector<double> xVals = {-4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0};
    const std::vector<double> yVals = {4.0, 3.0, 2.0, 1.0, 0.5, -0.5, -1.5, -2.5, -3.5};
    const int syntheticDegree       = ReadEnvInt("OPENFHE_CCFA_SYNTH_DEGREE", 0);
    const auto coeffs               = (syntheticDegree > 0) ? GetSyntheticCoeffs(syntheticDegree) : GetChebyshevCoeffs();
    const std::vector<double> xInput =
        (syntheticDegree > 0) ? std::vector<double>{-0.9, -0.6, -0.3, 0.0, 0.25, 0.5, 0.7, 0.85, 0.95} : xVals;
    const std::vector<double> yInput =
        (syntheticDegree > 0) ? std::vector<double>{0.95, 0.75, 0.55, 0.25, 0.0, -0.2, -0.45, -0.7, -0.9} : yVals;

    auto ptX = cc->MakeCKKSPackedPlaintext(ToComplex(xInput));
    auto ptY = cc->MakeCKKSPackedPlaintext(ToComplex(yInput));
    auto ctX = cc->Encrypt(kp.publicKey, ptX);
    auto ctY = cc->Encrypt(kp.publicKey, ptY);

    const auto exactX = (syntheticDegree > 0) ? EvalChebyshevPlain(coeffs, xInput) : std::vector<double>{};
    const auto exactY = (syntheticDegree > 0) ? EvalChebyshevPlain(coeffs, yInput) : std::vector<double>{};

    ClearCCFAEnv();
    auto xPowers = cc->EvalChebyPolys(ctX, coeffs, cfg.a, cfg.b);
    auto yPowers = cc->EvalChebyPolys(ctY, coeffs, cfg.a, cfg.b);

    const auto detStart = std::chrono::high_resolution_clock::now();
    auto baselineCX     = cc->EvalChebyshevSeriesWithPrecomp(xPowers, coeffs);
    auto baselineCY     = cc->EvalChebyshevSeriesWithPrecomp(yPowers, coeffs);
    const auto detStop  = std::chrono::high_resolution_clock::now();

    auto baselineX = DecryptReal(cc, kp.secretKey, baselineCX, xInput.size());
    auto baselineY = DecryptReal(cc, kp.secretKey, baselineCY, yInput.size());
    const auto& refX = (syntheticDegree > 0) ? exactX : baselineX;
    const auto& refY = (syntheticDegree > 0) ? exactY : baselineY;

    std::ofstream out(cfg.outputCsv);
    out << "mode,keep_prob,seed,success,latency_ms,output_error,product_error,cross_error,error,ring_dim,dcrt_bits,first_mod,mult_depth\n";

    const auto writeRow = [&](const TrialMetrics& row) {
        out << row.mode << ',' << std::fixed << std::setprecision(6) << row.keepProb << ',' << row.seed << ','
            << (row.success ? 1 : 0) << ',' << row.latencyMs << ',' << row.outputError << ',' << row.productError
            << ',' << row.crossError << ",\"" << row.error << "\"," << cfg.ringDim << ',' << cfg.dcrtBits << ','
            << cfg.firstMod << ',' << cfg.multDepth << '\n';
    };

    TrialMetrics det{"deterministic", 1.0, 0, true, 0.0, 0.0, 0.0, 0.0, ""};
    det.latencyMs = std::chrono::duration<double, std::milli>(detStop - detStart).count();
    det.outputError = 0.5 * (MeanAbsDiff(baselineX, refX) + MeanAbsDiff(baselineY, refY));
    det.productError = MeanAbsProductDiff(baselineX, baselineY, refX, refY);
    det.crossError = MeanAbsCrossError(baselineX, baselineY, refX, refY);
    writeRow(det);

    for (double keepProb : cfg.keepProbs) {
        for (uint64_t seed : cfg.seeds) {
            writeRow(RunTrial(cc, kp.secretKey, xPowers, yPowers, coeffs, refX, refY, "independent",
                              keepProb, seed));
            writeRow(
                RunTrial(cc, kp.secretKey, xPowers, yPowers, coeffs, refX, refY, "product", keepProb, seed));
        }
    }

    std::cout << "Wrote results to " << cfg.outputCsv << std::endl;
    return 0;
}
