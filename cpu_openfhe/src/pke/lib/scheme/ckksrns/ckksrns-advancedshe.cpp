//==================================================================================
// BSD 2-Clause License
//
// Copyright (c) 2014-2022, NJIT, Duality Technologies Inc. and other contributors
//
// All rights reserved.
//
// Author TPOC: contact@openfhe.org
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//==================================================================================

/*
CKKS implementation. See https://eprint.iacr.org/2020/1118 for details.
 */

#include "cryptocontext.h"
#include "scheme/ckksrns/ckksrns-cryptoparameters.h"
#include "scheme/ckksrns/ckksrns-advancedshe.h"
#include "scheme/ckksrns/ckksrns-utils.h"
#include "schemebase/base-scheme.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace lbcrypto {

namespace {

enum class CCFADistribution { Bernoulli, Gaussian, Uniform };

enum class CCFACouplingMode { Independent, WithinLevel, ProductCoupled, FullyCoupled };

struct CCFARandomConfig {
    std::string mode;
    double keepProb;
    uint64_t seed;
    uint32_t minM;
    uint32_t maxM;
    uint32_t protectHead;
    uint32_t protectTail;
    double eligibleRelAbs;
    double minScale;
    bool safeBootstrap;
    uint32_t safeMaxM;
    uint32_t safeDeterministicMinM;
    std::string safeBranches;
    bool disableCuInSafeMode;
    CCFADistribution distribution;
    CCFACouplingMode couplingMode;
};

struct CCFAProfileConfig {
    bool enabled;
    std::string outPath;
};

std::string LowerASCII(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

CCFADistribution ParseCCFADistribution(const char* raw) {
    const std::string value = raw ? LowerASCII(raw) : "bernoulli";
    if (value == "gaussian" || value == "normal") {
        return CCFADistribution::Gaussian;
    }
    if (value == "uniform") {
        return CCFADistribution::Uniform;
    }
    return CCFADistribution::Bernoulli;
}

CCFACouplingMode ParseCCFACouplingMode(const char* raw, const std::string& mode) {
    const std::string value = raw ? LowerASCII(raw) : LowerASCII(mode);
    if (value == "within" || value == "within_level" || value == "level") {
        return CCFACouplingMode::WithinLevel;
    }
    if (value == "fully" || value == "full" || value == "fully_coupled") {
        return CCFACouplingMode::FullyCoupled;
    }
    if (value == "product" || value == "product_coupled" || value == "product_safe") {
        return CCFACouplingMode::ProductCoupled;
    }
    return CCFACouplingMode::Independent;
}

CCFARandomConfig LoadCCFARandomConfigFromEnv() {
    CCFARandomConfig cfg;
    const char* modeEnv = std::getenv("OPENFHE_CCFA_MODE");
    cfg.mode            = modeEnv ? std::string(modeEnv) : "none";
    const char* keepEnv = std::getenv("OPENFHE_CCFA_KEEP_PROB");
    cfg.keepProb        = keepEnv ? std::stod(keepEnv) : 1.0;
    const char* seedEnv = std::getenv("OPENFHE_CCFA_SEED");
    cfg.seed            = seedEnv ? static_cast<uint64_t>(std::strtoull(seedEnv, nullptr, 10)) : 0ULL;
    const char* minMEnv = std::getenv("OPENFHE_CCFA_MIN_M");
    cfg.minM            = minMEnv ? static_cast<uint32_t>(std::strtoul(minMEnv, nullptr, 10)) : 2U;
    const char* maxMEnv = std::getenv("OPENFHE_CCFA_MAX_M");
    cfg.maxM            = maxMEnv ? static_cast<uint32_t>(std::strtoul(maxMEnv, nullptr, 10)) : 1000U;
    const char* protectHeadEnv = std::getenv("OPENFHE_CCFA_PROTECT_HEAD");
    cfg.protectHead            = protectHeadEnv ? static_cast<uint32_t>(std::strtoul(protectHeadEnv, nullptr, 10)) : 0U;
    const char* protectTailEnv = std::getenv("OPENFHE_CCFA_PROTECT_TAIL");
    cfg.protectTail            = protectTailEnv ? static_cast<uint32_t>(std::strtoul(protectTailEnv, nullptr, 10)) : 0U;
    const char* relAbsEnv      = std::getenv("OPENFHE_CCFA_ELIGIBLE_REL_ABS");
    cfg.eligibleRelAbs         = relAbsEnv ? std::stod(relAbsEnv) : 1.0;
    const char* minScaleEnv    = std::getenv("OPENFHE_CCFA_MIN_SCALE");
    cfg.minScale               = minScaleEnv ? std::stod(minScaleEnv) : 0.0;
    const char* safeEnv = std::getenv("OPENFHE_CCFA_SAFE_BOOTSTRAP");
    cfg.safeBootstrap   = safeEnv && std::string(safeEnv) == "1";
    const char* safeMaxMEnv = std::getenv("OPENFHE_CCFA_SAFE_MAX_M");
    cfg.safeMaxM            = safeMaxMEnv ? static_cast<uint32_t>(std::strtoul(safeMaxMEnv, nullptr, 10)) : 2U;
    const char* safeDetTopEnv = std::getenv("OPENFHE_CCFA_SAFE_DETERMINISTIC_MIN_M");
    cfg.safeDeterministicMinM =
        safeDetTopEnv ? static_cast<uint32_t>(std::strtoul(safeDetTopEnv, nullptr, 10)) : std::numeric_limits<uint32_t>::max();
    const char* safeBranchesEnv = std::getenv("OPENFHE_CCFA_SAFE_BRANCHES");
    cfg.safeBranches            = safeBranchesEnv ? LowerASCII(safeBranchesEnv) : "s";
    const char* disableCuEnv = std::getenv("OPENFHE_CCFA_SAFE_DISABLE_CU");
    cfg.disableCuInSafeMode  = !(disableCuEnv && std::string(disableCuEnv) == "0");
    cfg.distribution         = ParseCCFADistribution(std::getenv("OPENFHE_CCFA_DIST"));
    cfg.couplingMode         = ParseCCFACouplingMode(std::getenv("OPENFHE_CCFA_COUPLING"), cfg.mode);
    cfg.keepProb        = std::max(0.0, std::min(1.0, cfg.keepProb));
    cfg.eligibleRelAbs  = std::max(0.0, cfg.eligibleRelAbs);
    cfg.minScale        = std::max(0.0, std::min(1.0, cfg.minScale));
    return cfg;
}

CCFARandomConfig GetCCFARandomConfig() {
    auto envValue = [](const char* name) {
        const char* value = std::getenv(name);
        return value ? std::string(value) : std::string();
    };
    const std::string fingerprint = envValue("OPENFHE_CCFA_MODE") + "|" + envValue("OPENFHE_CCFA_KEEP_PROB") + "|" +
                                    envValue("OPENFHE_CCFA_SEED") + "|" + envValue("OPENFHE_CCFA_MIN_M") + "|" +
                                    envValue("OPENFHE_CCFA_MAX_M") + "|" + envValue("OPENFHE_CCFA_PROTECT_HEAD") +
                                    "|" + envValue("OPENFHE_CCFA_PROTECT_TAIL") + "|" +
                                    envValue("OPENFHE_CCFA_ELIGIBLE_REL_ABS") + "|" +
                                    envValue("OPENFHE_CCFA_MIN_SCALE") + "|" +
                                    envValue("OPENFHE_CCFA_SAFE_BOOTSTRAP") + "|" +
                                    envValue("OPENFHE_CCFA_SAFE_MAX_M") + "|" +
                                    envValue("OPENFHE_CCFA_SAFE_DETERMINISTIC_MIN_M") + "|" +
                                    envValue("OPENFHE_CCFA_SAFE_BRANCHES") + "|" +
                                    envValue("OPENFHE_CCFA_SAFE_DISABLE_CU") + "|" + envValue("OPENFHE_CCFA_DIST") +
                                    "|" + envValue("OPENFHE_CCFA_COUPLING");
    static std::string cachedFingerprint;
    static CCFARandomConfig cfg;
    if (fingerprint != cachedFingerprint) {
        cfg               = LoadCCFARandomConfigFromEnv();
        cachedFingerprint = fingerprint;
    }
    return cfg;
}

CCFAProfileConfig LoadCCFAProfileConfigFromEnv() {
    CCFAProfileConfig cfg;
    const char* outEnv = std::getenv("OPENFHE_CCFA_PROFILE_OUT");
    cfg.enabled        = outEnv && outEnv[0] != '\0';
    cfg.outPath        = cfg.enabled ? std::string(outEnv) : std::string();
    return cfg;
}

CCFAProfileConfig GetCCFAProfileConfig() {
    const char* outEnv = std::getenv("OPENFHE_CCFA_PROFILE_OUT");
    const std::string fingerprint = outEnv ? std::string(outEnv) : std::string();
    static std::string cachedFingerprint;
    static CCFAProfileConfig cfg;
    if (fingerprint != cachedFingerprint) {
        cfg               = LoadCCFAProfileConfigFromEnv();
        cachedFingerprint = fingerprint;
    }
    return cfg;
}

std::mutex& GetCCFAProfileMutex() {
    static std::mutex mutex;
    return mutex;
}

void AppendCCFAProfileRow(const std::string& region, const std::string& step, uint32_t m, double ms, int levelA = -1,
                          int levelB = -1, int towersA = -1, int towersB = -1, int candidateTerms = -1,
                          int nonzeroTerms = -1, int skippedTerms = -1) {
    const auto cfg = GetCCFAProfileConfig();
    if (!cfg.enabled) {
        return;
    }
    const auto randomCfg = GetCCFARandomConfig();

    std::lock_guard<std::mutex> guard(GetCCFAProfileMutex());
    const bool needHeader = !std::ifstream(cfg.outPath).good();
    std::ofstream out(cfg.outPath, std::ios::app);
    if (needHeader) {
        out << "mode,region,step,m,latency_ms,level_a,level_b,towers_a,towers_b,candidate_terms,nonzero_terms,"
               "skipped_terms\n";
    }
    out << randomCfg.mode << ',' << region << ',' << step << ',' << m << ',' << ms << ',' << levelA << ',' << levelB
        << ',' << towersA << ',' << towersB << ',' << candidateTerms << ',' << nonzeroTerms << ',' << skippedTerms
        << '\n';
}

uint64_t Mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

double HashToUnit(uint64_t seed, uint32_t m, const char* branch, uint32_t idx) {
    uint64_t h = Mix64(seed ^ (static_cast<uint64_t>(m) << 32) ^ static_cast<uint64_t>(idx));
    for (const char* p = branch; *p != '\0'; ++p) {
        h = Mix64(h ^ static_cast<uint64_t>(static_cast<unsigned char>(*p)));
    }
    return static_cast<double>(h >> 11) * (1.0 / static_cast<double>(1ULL << 53));
}

bool IsNotEqualNegOne(double value) {
    return std::abs(value + 1.0) > 1e-10;
}

double CCFASecondMoment(CCFADistribution distribution, double keepProb) {
    switch (distribution) {
        case CCFADistribution::Gaussian:
            return keepProb * keepProb + keepProb * (1.0 - keepProb);
        case CCFADistribution::Uniform:
            return (4.0 / 3.0) * keepProb * keepProb;
        case CCFADistribution::Bernoulli:
        default:
            return keepProb;
    }
}

double Normal01FromHashes(uint64_t seed, uint32_t m, const std::string& branchKey, uint32_t idx) {
    constexpr double TWO_PI = 6.283185307179586476925286766559;
    const double u1 = std::max(HashToUnit(seed, m, branchKey.c_str(), idx), 1e-12);
    const std::string branchKey2 = branchKey + ":gauss";
    const double u2 = HashToUnit(seed, m, branchKey2.c_str(), idx);
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(TWO_PI * u2);
}

double SampleCCFACoeffGate(const CCFARandomConfig& cfg, uint32_t m, const std::string& branchKey, uint32_t idx) {
    const double u = HashToUnit(cfg.seed, m, branchKey.c_str(), idx);
    switch (cfg.distribution) {
        case CCFADistribution::Gaussian: {
            const double variance = std::max(0.0, cfg.keepProb * (1.0 - cfg.keepProb));
            return cfg.keepProb + std::sqrt(variance) * Normal01FromHashes(cfg.seed, m, branchKey, idx);
        }
        case CCFADistribution::Uniform:
            return 2.0 * cfg.keepProb * u;
        case CCFADistribution::Bernoulli:
        default:
            return (u <= cfg.keepProb) ? 1.0 : 0.0;
    }
}

std::pair<std::string, uint32_t> CCFACouplingKey(const CCFARandomConfig& cfg, const char* branch, uint32_t idx) {
    const std::string branchName = branch;
    switch (cfg.couplingMode) {
        case CCFACouplingMode::WithinLevel:
            return {"level", idx};
        case CCFACouplingMode::ProductCoupled:
            if (branchName == "q" || branchName == "s") {
                return {"qs", idx};
            }
            return {branchName, idx};
        case CCFACouplingMode::FullyCoupled:
            return {"all", 0};
        case CCFACouplingMode::Independent:
        default:
            return {branchName, idx};
    }
}

double CCFAEffectiveScale(const CCFARandomConfig& cfg, const char* branch) {
    const std::string branchName = branch;
    const bool productPair = cfg.couplingMode == CCFACouplingMode::ProductCoupled &&
                             (branchName == "q" || branchName == "s");
    const double baseScale = productPair ? std::sqrt(CCFASecondMoment(cfg.distribution, cfg.keepProb)) : cfg.keepProb;
    return std::max(baseScale, cfg.minScale);
}

bool CCFAAllowsSafeBranch(const CCFARandomConfig& cfg, const std::string& branchName) {
    const std::string branches = "," + cfg.safeBranches + ",";
    return branches.find("," + LowerASCII(branchName) + ",") != std::string::npos;
}

template <typename VectorDataType>
VectorDataType ScaleCoeffForCCFA(const VectorDataType& value, double scale) {
    if (scale == 1.0)
        return value;
    if constexpr (std::is_integral_v<VectorDataType>) {
        return static_cast<VectorDataType>(std::llround(static_cast<double>(value) / scale));
    }
    else {
        return value / scale;
    }
}

template <typename VectorDataType>
double AbsCoeffForCCFA(const VectorDataType& value) {
    if constexpr (std::is_integral_v<VectorDataType>) {
        return std::abs(static_cast<double>(value));
    }
    else {
        return std::abs(value);
    }
}

template <typename VectorDataType>
void ApplyCCFARandomResidual(std::vector<VectorDataType>& coeffs, uint32_t m, const char* branch) {
    const auto cfg = GetCCFARandomConfig();
    if (cfg.mode == "none" || cfg.keepProb >= 1.0 || coeffs.size() <= 2 || m < cfg.minM || m > cfg.maxM)
        return;
    if (cfg.safeBootstrap) {
        if (m >= cfg.safeDeterministicMinM) {
            return;
        }
        if (m > cfg.safeMaxM) {
            return;
        }
        const std::string branchName = branch;
        if (!CCFAAllowsSafeBranch(cfg, branchName)) {
            return;
        }
        if (cfg.disableCuInSafeMode && branchName == "cu") {
            return;
        }
    }

    const double effectiveScale = CCFAEffectiveScale(cfg, branch);
    const size_t firstEligible  = std::min<size_t>(coeffs.size() - 1, 1 + cfg.protectHead);
    const size_t lastExclusive =
        (coeffs.size() > (1 + cfg.protectTail)) ? (coeffs.size() - 1 - cfg.protectTail) : static_cast<size_t>(1);

    double maxAbs = 0.0;
    for (size_t i = firstEligible; i < lastExclusive; ++i) {
        if (coeffs[i] == VectorDataType{}) {
            continue;
        }
        maxAbs = std::max(maxAbs, AbsCoeffForCCFA(coeffs[i]));
    }
    const double absThreshold = maxAbs * cfg.eligibleRelAbs;

    uint32_t candidateTerms = 0;
    uint32_t keptTerms      = 0;
    uint32_t zeroedTerms    = 0;
    for (size_t i = 1; i + 1 < coeffs.size(); ++i) {
        if (coeffs[i] == VectorDataType{})
            continue;
        if (i < firstEligible || i >= lastExclusive) {
            continue;
        }
        if (AbsCoeffForCCFA(coeffs[i]) > absThreshold) {
            continue;
        }
        ++candidateTerms;
        const auto key = CCFACouplingKey(cfg, branch, static_cast<uint32_t>(i));
        const double gate = SampleCCFACoeffGate(cfg, m, key.first, key.second);
        if (effectiveScale <= 0.0 || gate == 0.0) {
            coeffs[i] = VectorDataType{};
            ++zeroedTerms;
        }
        else {
            coeffs[i] = ScaleCoeffForCCFA(coeffs[i], effectiveScale / gate);
            ++keptTerms;
        }
    }
    AppendCCFAProfileRow("ccfa_randomizer", branch, m, 0.0, -1, -1, -1, -1, static_cast<int>(candidateTerms),
                         static_cast<int>(keptTerms), static_cast<int>(zeroedTerms));
}

}  // namespace

Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalMultMany(const std::vector<Ciphertext<DCRTPoly>>& ciphertextVec,
                                                      const std::vector<EvalKey<DCRTPoly>>& evalKeys) const {
    const uint32_t inSize = ciphertextVec.size();

    if (inSize == 0)
        OPENFHE_THROW("Input ciphertext vector is empty.");

    if (inSize == 1)
        return ciphertextVec[0]->Clone();

    const uint32_t lim = inSize * 2 - 2;
    std::vector<Ciphertext<DCRTPoly>> ciphertextMultVec(inSize - 1);

    auto algo               = ciphertextVec[0]->GetCryptoContext()->GetScheme();
    const auto cryptoParams = std::dynamic_pointer_cast<CryptoParametersRNS>(ciphertextVec[0]->GetCryptoParameters());
    uint32_t levelsToDrop   = cryptoParams->GetCompositeDegree();

    uint32_t i = 0, j = 0;
    for (; i < (inSize - 1); i += 2) {
        ciphertextMultVec[j] = algo->EvalMultAndRelinearize(ciphertextVec[i], ciphertextVec[i + 1], evalKeys);
        algo->ModReduceInPlace(ciphertextMultVec[j++], levelsToDrop);
    }
    if (i < inSize) {
        ciphertextMultVec[j] =
            algo->EvalMultAndRelinearize(ciphertextVec[i], ciphertextMultVec[i + 1 - inSize], evalKeys);
        algo->ModReduceInPlace(ciphertextMultVec[j++], levelsToDrop);
        i += 2;
    }
    for (; i < lim; i += 2) {
        ciphertextMultVec[j] =
            algo->EvalMultAndRelinearize(ciphertextMultVec[i - inSize], ciphertextMultVec[i + 1 - inSize], evalKeys);
        algo->ModReduceInPlace(ciphertextMultVec[j++], levelsToDrop);
    }

    return ciphertextMultVec.back();
}

//------------------------------------------------------------------------------
// LINEAR WEIGHTED SUM
//------------------------------------------------------------------------------

template <typename VectorDataType>
Ciphertext<DCRTPoly> internalEvalLinearWSum(const std::vector<ReadOnlyCiphertext<DCRTPoly>>& ciphertexts,
                                            const std::vector<VectorDataType>& constants) {
    const uint32_t limit = ciphertexts.size();
    std::vector<Ciphertext<DCRTPoly>> cts(limit);
    for (uint32_t i = 0; i < limit; ++i)
        cts[i] = ciphertexts[i]->Clone();
    return internalEvalLinearWSumMutable(cts, constants);
}

template <typename VectorDataType>
Ciphertext<DCRTPoly> internalEvalLinearWSumMutable(std::vector<Ciphertext<DCRTPoly>>& ciphertexts,
                                                   const std::vector<VectorDataType>& constants) {
    const auto cryptoParams = std::dynamic_pointer_cast<CryptoParametersCKKSRNS>(ciphertexts[0]->GetCryptoParameters());

    auto cc = ciphertexts[0]->GetCryptoContext();

    const uint32_t limit = ciphertexts.size();

    if (cryptoParams->GetScalingTechnique() != FIXEDMANUAL) {
        // Check to see if input ciphertexts are of same level
        // and adjust if needed to the max level among them
        uint32_t maxLevel = ciphertexts[0]->GetLevel();
        uint32_t maxIdx   = 0;
        for (uint32_t i = 1; i < limit; ++i) {
            if ((ciphertexts[i]->GetLevel() > maxLevel) ||
                ((ciphertexts[i]->GetLevel() == maxLevel) && (ciphertexts[i]->GetNoiseScaleDeg() == 2))) {
                maxLevel = ciphertexts[i]->GetLevel();
                maxIdx   = i;
            }
        }

        auto algo = cc->GetScheme();
        for (uint32_t i = 0; i < maxIdx; ++i)
            algo->AdjustLevelsAndDepthInPlace(ciphertexts[i], ciphertexts[maxIdx]);
        for (uint32_t i = maxIdx + 1; i < limit; ++i)
            algo->AdjustLevelsAndDepthInPlace(ciphertexts[i], ciphertexts[maxIdx]);

        uint32_t compositeDegree = cryptoParams->GetCompositeDegree();
        if (ciphertexts[maxIdx]->GetNoiseScaleDeg() == 2) {
            for (uint32_t i = 0; i < limit; ++i)
                algo->ModReduceInternalInPlace(ciphertexts[i], compositeDegree);
        }
    }

    cc->EvalMultInPlace(ciphertexts[0], constants[0]);
    for (uint32_t i = 1; i < limit; ++i) {
        cc->EvalMultInPlace(ciphertexts[i], constants[i]);
        cc->EvalAddInPlaceNoCheck(ciphertexts[0], ciphertexts[i]);
    }
    cc->ModReduceInPlace(ciphertexts[0]);
    return ciphertexts[0];
}

template <typename VectorDataType>
Ciphertext<DCRTPoly> EvalPartialLinearWSum(const std::vector<Ciphertext<DCRTPoly>>& ciphertexts,
                                           const std::vector<VectorDataType>& constants, uint32_t limit = 0) {
    if (0 == limit)
        limit = ciphertexts.size();

    std::vector<uint32_t> active;
    active.reserve(limit);
    for (uint32_t i = 0; i < limit; ++i) {
        if (IsNotEqualZero(constants[i + 1])) {
            active.push_back(i);
        }
    }
    AppendCCFAProfileRow("linear_wsum", "sparse_terms", limit, 0.0, -1, -1, -1, -1, static_cast<int>(limit),
                         static_cast<int>(active.size()), static_cast<int>(limit - active.size()));

    const auto cryptoParams = std::dynamic_pointer_cast<CryptoParametersCKKSRNS>(ciphertexts[0]->GetCryptoParameters());

    auto cc = ciphertexts[0]->GetCryptoContext();

    if (active.empty()) {
        auto zero = ciphertexts[0]->Clone();
        cc->EvalMultInPlace(zero, 0.0);
        cc->ModReduceInPlace(zero);
        return zero;
    }

    const uint32_t activeLimit = active.size();
    std::vector<Ciphertext<DCRTPoly>> cts(activeLimit);
    if (cryptoParams->GetScalingTechnique() != FIXEDMANUAL) {
        cts[0] = ciphertexts[active[0]]->Clone();
        // Check to see if input ciphertexts are of same level
        // and adjust if needed to the max level among them
        uint32_t maxLevel = cts[0]->GetLevel();
        uint32_t maxIdx   = 0;
        for (uint32_t i = 1; i < activeLimit; ++i) {
            cts[i] = ciphertexts[active[i]]->Clone();
            if ((cts[i]->GetLevel() > maxLevel) ||
                ((cts[i]->GetLevel() == maxLevel) && (cts[i]->GetNoiseScaleDeg() == 2))) {
                maxLevel = cts[i]->GetLevel();
                maxIdx   = i;
            }
        }

        auto algo = cc->GetScheme();
        auto& ctm = cts[maxIdx];
        for (uint32_t i = 0; i < maxIdx; ++i)
            algo->AdjustLevelsAndDepthInPlace(cts[i], ctm);
        for (uint32_t i = maxIdx + 1; i < activeLimit; ++i)
            algo->AdjustLevelsAndDepthInPlace(cts[i], ctm);

        uint32_t compositeDegree = cryptoParams->GetCompositeDegree();
        if (ctm->GetNoiseScaleDeg() == 2) {
            for (uint32_t i = 0; i < activeLimit; ++i)
                algo->ModReduceInternalInPlace(cts[i], compositeDegree);
        }
    }
    else {
        for (uint32_t i = 0; i < activeLimit; ++i)
            cts[i] = ciphertexts[active[i]]->Clone();
    }

    cc->EvalMultInPlace(cts[0], constants[active[0] + 1]);
    for (uint32_t i = 1; i < activeLimit; ++i) {
        cc->EvalMultInPlace(cts[i], constants[active[i] + 1]);
        cc->EvalAddInPlaceNoCheck(cts[0], cts[i]);
    }
    cc->ModReduceInPlace(cts[0]);
    return cts[0];
}

Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalLinearWSum(std::vector<ReadOnlyCiphertext<DCRTPoly>>& ciphertexts,
                                                        const std::vector<int64_t>& constants) const {
    return internalEvalLinearWSum(ciphertexts, constants);
}
Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalLinearWSum(std::vector<ReadOnlyCiphertext<DCRTPoly>>& ciphertexts,
                                                        const std::vector<double>& constants) const {
    return internalEvalLinearWSum(ciphertexts, constants);
}
Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalLinearWSum(std::vector<ReadOnlyCiphertext<DCRTPoly>>& ciphertexts,
                                                        const std::vector<std::complex<double>>& constants) const {
    return internalEvalLinearWSum(ciphertexts, constants);
}

Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalLinearWSumMutable(std::vector<Ciphertext<DCRTPoly>>& ciphertexts,
                                                               const std::vector<int64_t>& constants) const {
    return internalEvalLinearWSumMutable(ciphertexts, constants);
}
Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalLinearWSumMutable(std::vector<Ciphertext<DCRTPoly>>& ciphertexts,
                                                               const std::vector<double>& constants) const {
    return internalEvalLinearWSumMutable(ciphertexts, constants);
}
Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalLinearWSumMutable(
    std::vector<Ciphertext<DCRTPoly>>& ciphertexts, const std::vector<std::complex<double>>& constants) const {
    return internalEvalLinearWSumMutable(ciphertexts, constants);
}

//------------------------------------------------------------------------------
// EVAL POLYNOMIAL
//------------------------------------------------------------------------------

template <typename VectorDataType>
std::shared_ptr<seriesPowers<DCRTPoly>> internalEvalPowersLinear(ConstCiphertext<DCRTPoly>& x,
                                                                 const std::vector<VectorDataType>& coefficients) {
    const uint32_t k = coefficients.size() - 1;
    std::vector<bool> indices(k);

    // find indices for powers of x that need to be computed
    for (uint32_t i = k; i > 0; --i) {
        if (0 == (i & (i - 1))) {  // if i is a power of 2
            indices[i - 1] = true;
        }
        else {  // non-power of 2
            if (IsNotEqualZero(coefficients[i])) {
                uint32_t rem = i;

                // while rem is not a power of 2
                // set indices required to compute rem to 1
                while (0 != (rem & (rem - 1))) {
                    indices[rem - 1] = true;
                    rem &= (uint64_t(1) << (GetMSB(rem) - 1)) - 1;
                }
            }
        }
    }

    std::vector<Ciphertext<DCRTPoly>> powers(k);
    powers[0] = x->Clone();
    auto cc   = x->GetCryptoContext();

    auto cryptoParams        = std::dynamic_pointer_cast<CryptoParametersCKKSRNS>(x->GetCryptoParameters());
    uint32_t compositeDegree = cryptoParams->GetCompositeDegree();

    // computes all powers up to k for x
    for (uint32_t i = 2; i <= k; ++i) {
        if (0 == (i & (i - 1))) {
            powers[i - 1] = cc->EvalSquare(powers[i / 2 - 1]);
            cc->ModReduceInPlace(powers[i - 1]);
        }
        else {
            if (indices[i - 1]) {
                uint64_t p    = (uint64_t(1) << (GetMSB(i) - 1)) - 1;
                uint64_t r    = (i & p) - 1;
                uint32_t diff = powers[p]->GetLevel() - powers[r]->GetLevel();
                cc->LevelReduceInPlace(powers[r], nullptr, diff / compositeDegree);

                powers[i - 1] = cc->EvalMult(powers[p], powers[r]);
                cc->ModReduceInPlace(powers[i - 1]);
            }
        }
    }

    // brings all powers of x to the same level
    for (uint32_t i = 1; i < k; ++i) {
        if (indices[i - 1]) {
            uint32_t diff = powers[k - 1]->GetLevel() - powers[i - 1]->GetLevel();
            cc->LevelReduceInPlace(powers[i - 1], nullptr, diff / compositeDegree);
        }
    }

    return std::make_shared<seriesPowers<DCRTPoly>>(std::move(powers));
}

std::shared_ptr<seriesPowers<DCRTPoly>> internalEvalPowersPS(ConstCiphertext<DCRTPoly>& x, uint32_t degree) {
    auto degs  = ComputeDegreesPS(degree);
    uint32_t k = degs[0];
    uint32_t m = degs[1];

    std::vector<Ciphertext<DCRTPoly>> powers(k);
    powers[0] = x->Clone();

    auto cc = x->GetCryptoContext();
    uint32_t compositeDegree =
        std::dynamic_pointer_cast<CryptoParametersCKKSRNS>(x->GetCryptoParameters())->GetCompositeDegree();

    // computes all powers up to k for x
    uint32_t powerOf2 = 2;
    uint32_t rem      = 0;
    for (uint32_t i = 2; i <= k; ++i) {
        if (rem == 0) {
            powers[i - 1] = cc->EvalSquare(powers[(powerOf2 >> 1) - 1]);
        }
        else {
            uint32_t diff = powers[powerOf2 - 1]->GetLevel() - powers[rem - 1]->GetLevel();
            cc->LevelReduceInPlace(powers[rem - 1], nullptr, diff / compositeDegree);
            powers[i - 1] = cc->EvalMult(powers[powerOf2 - 1], powers[rem - 1]);
        }

        if (++rem == powerOf2) {
            powerOf2 <<= 1;
            rem = 0;
        }
        cc->ModReduceInPlace(powers[i - 1]);
    }

    const auto cryptoParams = std::dynamic_pointer_cast<CryptoParametersCKKSRNS>(powers[k - 1]->GetCryptoParameters());
    if (cryptoParams->GetScalingTechnique() == FIXEDMANUAL) {
        // brings all powers of x to the same level
        uint32_t levelk = powers[k - 1]->GetLevel();
        for (uint32_t i = 1; i < k; ++i)
            cc->LevelReduceInPlace(powers[i - 1], nullptr, levelk - powers[i - 1]->GetLevel());
    }
    else {
        for (uint32_t i = 1; i < k; ++i)
            cc->GetScheme()->AdjustLevelsAndDepthInPlace(powers[i - 1], powers[k - 1]);
    }

    // computes powers of form k*2^i for x and the product of the powers in power2, that yield x^{k(2*m - 1)}
    std::vector<Ciphertext<DCRTPoly>> powers2(m);
    powers2[0] = powers.back();

    auto power2km1 = powers.back();

    for (uint32_t i = 1; i < m; ++i) {
        powers2[i] = cc->EvalSquare(powers2[i - 1]);
        cc->ModReduceInPlace(powers2[i]);
        power2km1 = cc->EvalMult(powers2[i], power2km1);
        cc->ModReduceInPlace(power2km1);
    }

    return std::make_shared<seriesPowers<DCRTPoly>>(std::move(powers), std::move(powers2), std::move(power2km1), k, m);
}

std::shared_ptr<seriesPowers<DCRTPoly>> AdvancedSHECKKSRNS::EvalPowers(ConstCiphertext<DCRTPoly>& x,
                                                                       const std::vector<int64_t>& coefficients) const {
    uint32_t d = Degree(coefficients);
    return (d < 5) ? internalEvalPowersLinear(x, coefficients) : internalEvalPowersPS(x, d);
}
std::shared_ptr<seriesPowers<DCRTPoly>> AdvancedSHECKKSRNS::EvalPowers(ConstCiphertext<DCRTPoly>& x,
                                                                       const std::vector<double>& coefficients) const {
    uint32_t d = Degree(coefficients);
    return (d < 5) ? internalEvalPowersLinear(x, coefficients) : internalEvalPowersPS(x, d);
}
std::shared_ptr<seriesPowers<DCRTPoly>> AdvancedSHECKKSRNS::EvalPowers(
    ConstCiphertext<DCRTPoly>& x, const std::vector<std::complex<double>>& coefficients) const {
    uint32_t d = Degree(coefficients);
    return (d < 5) ? internalEvalPowersLinear(x, coefficients) : internalEvalPowersPS(x, d);
}

template <typename VectorDataType>
static inline Ciphertext<DCRTPoly> internalEvalPolyLinearWithPrecomp(std::vector<Ciphertext<DCRTPoly>>& powers,
                                                                     const std::vector<VectorDataType>& coefficients) {
    if (coefficients.size() < 2)
        OPENFHE_THROW("The coefficients vector should contain at least 2 elements");

    const uint32_t k = coefficients.size() - 1;
    if (!IsNotEqualZero(coefficients[k]))
        OPENFHE_THROW("The highest-order coefficient cannot be set to 0");

    auto cc = powers[0]->GetCryptoContext();

    // perform scalar multiplication for the highest-order term
    auto result = cc->EvalMult(powers[k - 1], coefficients[k]);

    // perform scalar multiplication for all other terms and sum them up
    for (uint32_t i = 1; i < k; ++i) {
        if (IsNotEqualZero(coefficients[i])) {
            cc->EvalMultInPlace(powers[i - 1], coefficients[i]);
            cc->EvalAddInPlace(result, powers[i - 1]);
        }
    }

    // Do rescaling after scalar multiplication
    cc->ModReduceInPlace(result);

    // adds the free term (at x^0)
    cc->EvalAddInPlace(result, coefficients[0]);

    return result;
}

template <typename VectorDataType>
Ciphertext<DCRTPoly> InnerEvalPolyPS(ConstCiphertext<DCRTPoly>& x, const std::vector<VectorDataType>& coefficients,
                                     uint32_t k, uint32_t m, const std::vector<Ciphertext<DCRTPoly>>& powers,
                                     const std::vector<Ciphertext<DCRTPoly>>& powers2) {
    // Compute k*2^m because we use it often
    uint32_t k2m2k = k * (1 << (m - 1)) - k;

    // Divide coefficients by x^{k*2^{m-1}}
    std::vector<VectorDataType> xkm(k2m2k + k + 1);
    xkm.back() = 1;
    auto divqr = LongDivisionPoly(coefficients, xkm);

    // Subtract x^{k(2^{m-1} - 1)} from r
    auto& r2 = divqr->r;
    if (auto n = Degree(r2); static_cast<int32_t>(k2m2k - n) <= 0) {
        r2.resize(n + 1);
        r2[k2m2k] -= 1;
    }
    else {
        r2.resize(k2m2k + 1);
        r2.back() = -1;
    }

    auto divcs = LongDivisionPoly(r2, divqr->q);
    auto cc    = x->GetCryptoContext();

    Ciphertext<DCRTPoly> cu, qu, su;

#pragma omp task shared(qu)
    {
        // Evaluate q and s2 at u.
        // If their degrees are larger than k, then recursively apply the Paterson-Stockmeyer algorithm.

        if (Degree(divqr->q) > k) {
            qu = InnerEvalPolyPS(x, divqr->q, k, m - 1, powers, powers2);
        }
        else {
            qu = cc->EvalAdd(powers[k - 1], divqr->q.front());
            divqr->q.resize(k);
            if (uint32_t n = Degree(divqr->q); n > 0)
                cc->EvalAddInPlace(qu, EvalPartialLinearWSum(powers, divqr->q, n));
        }
    }

#pragma omp task shared(su)
    {
        // Add x^{k(2^{m-1} - 1)} to s
        auto& s2 = divcs->r;
        s2.resize(k2m2k + 1);
        s2.back() = 1;

        if (Degree(s2) > k) {
            su = InnerEvalPolyPS(x, s2, k, m - 1, powers, powers2);
        }
        else {
            su = cc->EvalAdd(powers[k - 1], s2.front());
            s2.resize(k);
            if (uint32_t n = Degree(s2); n > 0)
                cc->EvalAddInPlace(su, EvalPartialLinearWSum(powers, s2, n));
        }
    }

    if (uint32_t n = Degree(divcs->q); n == 0) {
        cu = cc->EvalAdd(powers2[m - 1], divcs->q.front());
    }
    else if (n == 1) {
        if (IsNotEqualOne(divcs->q[1])) {
            cu = cc->EvalMult(powers.front(), divcs->q[1]);
            cc->ModReduceInPlace(cu);
            cc->EvalAddInPlace(cu, powers2[m - 1]);
        }
        else {
            cu = cc->EvalAdd(powers2[m - 1], powers.front());
        }
        cc->EvalAddInPlace(cu, divcs->q.front());
    }
    else {
        cu = cc->EvalAdd(powers2[m - 1], EvalPartialLinearWSum(powers, divcs->q, n));
        cc->EvalAddInPlace(cu, divcs->q.front());
    }

#pragma omp taskwait

    auto result = cc->EvalMult(cu, qu);
    cc->ModReduceInPlace(result);
    cc->EvalAddInPlace(result, su);
    return result;
}

template <typename VectorDataType>
Ciphertext<DCRTPoly> internalEvalPolyPSWithPrecomp(const std::shared_ptr<seriesPowers<DCRTPoly>>& ctxtPowers,
                                                   const std::vector<VectorDataType>& coefficients) {
    auto& powers    = ctxtPowers->powersRe;
    auto& powers2   = ctxtPowers->powers2Re;
    auto& power2km1 = ctxtPowers->power2km1Re;
    auto k          = ctxtPowers->k;
    auto m          = ctxtPowers->m;

    // Compute k*2^{m-1}-k because we use it a lot
    uint32_t k2m2k = k * (1 << (m - 1)) - k;

    // Add T^{k(2^m - 1)}(y) to the polynomial that has to be evaluated
    auto f2 = coefficients;
    f2.resize(Degree(f2) + 1);
    f2.resize(2 * k2m2k + k + 1);
    f2.back() = 1;

    Ciphertext<DCRTPoly> result;
#pragma omp parallel num_threads(OpenFHEParallelControls.GetThreadLimit(6 * m + 2))
    {
#pragma omp single
        result =
            powers[0]->GetCryptoContext()->EvalSub(InnerEvalPolyPS(powers[0], f2, k, m, powers, powers2), power2km1);
    }
    return result;
}

Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalPoly(ConstCiphertext<DCRTPoly>& x,
                                                  const std::vector<int64_t>& coeffs) const {
    return (Degree(coeffs) < 5) ? EvalPolyLinear(x, coeffs) : EvalPolyPS(x, coeffs);
}
Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalPoly(ConstCiphertext<DCRTPoly>& x,
                                                  const std::vector<double>& coeffs) const {
    return (Degree(coeffs) < 5) ? EvalPolyLinear(x, coeffs) : EvalPolyPS(x, coeffs);
}
Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalPoly(ConstCiphertext<DCRTPoly>& x,
                                                  const std::vector<std::complex<double>>& coeffs) const {
    return (Degree(coeffs) < 5) ? EvalPolyLinear(x, coeffs) : EvalPolyPS(x, coeffs);
}

Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalPolyWithPrecomp(std::shared_ptr<seriesPowers<DCRTPoly>> ctxtPowers,
                                                             const std::vector<int64_t>& coeffs) const {
    return (Degree(coeffs) < 5) ? internalEvalPolyLinearWithPrecomp(ctxtPowers->powersRe, coeffs) :
                                  internalEvalPolyPSWithPrecomp(ctxtPowers, coeffs);
}
Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalPolyWithPrecomp(std::shared_ptr<seriesPowers<DCRTPoly>> ctxtPowers,
                                                             const std::vector<double>& coeffs) const {
    return (Degree(coeffs) < 5) ? internalEvalPolyLinearWithPrecomp(ctxtPowers->powersRe, coeffs) :
                                  internalEvalPolyPSWithPrecomp(ctxtPowers, coeffs);
}
Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalPolyWithPrecomp(std::shared_ptr<seriesPowers<DCRTPoly>> ctxtPowers,
                                                             const std::vector<std::complex<double>>& coeffs) const {
    return (Degree(coeffs) < 5) ? internalEvalPolyLinearWithPrecomp(ctxtPowers->powersRe, coeffs) :
                                  internalEvalPolyPSWithPrecomp(ctxtPowers, coeffs);
}

Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalPolyLinear(ConstCiphertext<DCRTPoly>& x,
                                                        const std::vector<int64_t>& coeffs) const {
    return internalEvalPolyLinearWithPrecomp(internalEvalPowersLinear(x, coeffs)->powersRe, coeffs);
}
Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalPolyLinear(ConstCiphertext<DCRTPoly>& x,
                                                        const std::vector<double>& coeffs) const {
    return internalEvalPolyLinearWithPrecomp(internalEvalPowersLinear(x, coeffs)->powersRe, coeffs);
}
Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalPolyLinear(ConstCiphertext<DCRTPoly>& x,
                                                        const std::vector<std::complex<double>>& coeffs) const {
    return internalEvalPolyLinearWithPrecomp(internalEvalPowersLinear(x, coeffs)->powersRe, coeffs);
}

Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalPolyPS(ConstCiphertext<DCRTPoly>& x,
                                                    const std::vector<int64_t>& coeffs) const {
    return internalEvalPolyPSWithPrecomp(internalEvalPowersPS(x, Degree(coeffs)), coeffs);
}
Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalPolyPS(ConstCiphertext<DCRTPoly>& x,
                                                    const std::vector<double>& coeffs) const {
    return internalEvalPolyPSWithPrecomp(internalEvalPowersPS(x, Degree(coeffs)), coeffs);
}
Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalPolyPS(ConstCiphertext<DCRTPoly>& x,
                                                    const std::vector<std::complex<double>>& coeffs) const {
    return internalEvalPolyPSWithPrecomp(internalEvalPowersPS(x, Degree(coeffs)), coeffs);
}

//------------------------------------------------------------------------------
// EVAL CHEBYSHEV SERIES
//------------------------------------------------------------------------------

template <typename VectorDataType>
std::shared_ptr<seriesPowers<DCRTPoly>> internalEvalChebyPolysLinear(ConstCiphertext<DCRTPoly>& x,
                                                                     const std::vector<VectorDataType>& coefficients,
                                                                     double a, double b) {
    const uint32_t k = coefficients.size() - 1;
    std::vector<Ciphertext<DCRTPoly>> T(k);

    auto cc = x->GetCryptoContext();

    // computes linear transformation y = -1 + 2 (x-a)/(b-a)
    // consumes one level when a <> -1 && b <> 1
    if (!IsNotEqualNegOne(a) && !IsNotEqualOne(b)) {
        T[0] = x->Clone();
    }
    else {
        // linear transformation is needed
        double alpha = 2 / (b - a);
        double beta  = a * alpha;

        T[0] = cc->EvalMult(x, alpha);
        cc->ModReduceInPlace(T[0]);
        cc->EvalAddInPlace(T[0], -1.0 - beta);
    }

    // Computes Chebyshev polynomials up to degree k
    // for y: T_1(y) = y, T_2(y), ... , T_k(y)
    // uses binary tree multiplication
    for (uint32_t i = 2; i <= k; ++i) {
        if (i & 0x1) {  // if i is odd
            // compute T_{2i+1}(y) = 2*T_i(y)*T_{i+1}(y) - y
            T[i - 1] = cc->EvalMult(T[i / 2 - 1], T[i / 2]);
            cc->EvalAddInPlaceNoCheck(T[i - 1], T[i - 1]);
            cc->ModReduceInPlace(T[i - 1]);
            cc->EvalSubInPlace(T[i - 1], T[0]);
        }
        else {
            // compute T_{2i}(y) = 2*T_i(y)^2 - 1
            T[i - 1] = cc->EvalSquare(T[i / 2 - 1]);
            cc->EvalAddInPlaceNoCheck(T[i - 1], T[i - 1]);
            cc->ModReduceInPlace(T[i - 1]);
            cc->EvalAddInPlace(T[i - 1], -1.0);
        }
    }

    uint32_t compositeDegree =
        std::dynamic_pointer_cast<CryptoParametersCKKSRNS>(x->GetCryptoParameters())->GetCompositeDegree();
    for (uint32_t i = 1; i < k; ++i)
        cc->LevelReduceInPlace(T[i - 1], nullptr, (T[k - 1]->GetLevel() - T[i - 1]->GetLevel()) / compositeDegree);

    return std::make_shared<seriesPowers<DCRTPoly>>(std::move(T));
}

template <typename VectorDataType>
Ciphertext<DCRTPoly> internalEvalChebyshevSeriesLinearWithPrecomp(std::vector<Ciphertext<DCRTPoly>>& T,
                                                                  const std::vector<VectorDataType>& coefficients) {
    const uint32_t k = coefficients.size() - 2;

    // perform scalar multiplication for the highest-order term
    auto cc     = T[0]->GetCryptoContext();
    auto result = cc->EvalMult(T[k], coefficients[k + 1]);

    // perform scalar multiplication for all other terms and sum them up
    uint32_t nonzeroTerms = 1;
    for (uint32_t i = 0; i < k; ++i) {
        if (IsNotEqualZero(coefficients[i + 1])) {
            ++nonzeroTerms;
            cc->EvalMultInPlace(T[i], coefficients[i + 1]);
            cc->EvalAddInPlace(result, T[i]);
        }
    }
    AppendCCFAProfileRow("chebyshev_linear", "sparse_terms", k + 1, 0.0, -1, -1, -1, -1,
                         static_cast<int>(k + 1), static_cast<int>(nonzeroTerms),
                         static_cast<int>((k + 1) - nonzeroTerms));

    // Do rescaling after scalar multiplication
    cc->ModReduceInPlace(result);

    // adds the free term (at x^0)
    cc->EvalAddInPlace(result, coefficients[0] / 2.0);

    return result;
}

template <typename VectorDataType>
Ciphertext<DCRTPoly> InnerEvalChebyshevPS(ConstCiphertext<DCRTPoly>& x, const std::vector<VectorDataType>& coefficients,
                                          uint32_t k, uint32_t m, const std::vector<Ciphertext<DCRTPoly>>& T,
                                          const std::vector<Ciphertext<DCRTPoly>>& T2) {
    const auto nowMs = []() {
        return std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now().time_since_epoch())
            .count();
    };

    // Compute k*2^{m-1}-k because we use it a lot
    uint32_t k2m2k = k * (1 << (m - 1)) - k;

    // Divide coefficients by T^{k*2^{m-1}}
    std::vector<VectorDataType> Tkm(k2m2k + k + 1);
    Tkm.back() = 1;
    auto divqr = LongDivisionChebyshev(coefficients, Tkm);

    // Subtract x^{k(2^{m-1} - 1)} from r
    auto& r2 = divqr->r;
    if (uint32_t n = Degree(r2); static_cast<int32_t>(k2m2k - n) <= 0) {
        r2.resize(n + 1);
        r2[k2m2k] -= 1;
    }
    else {
        r2.resize(k2m2k + 1);
        r2.back() = -1;
    }

    auto divcs = LongDivisionChebyshev(r2, divqr->q);
    auto cc    = x->GetCryptoContext();

    auto qEval = divqr->q;
    auto sEval = divcs->r;
    sEval.resize(k2m2k + 1);
    sEval.back() = 1;
    auto cEval = divcs->q;

    ApplyCCFARandomResidual(qEval, m, "q");
    ApplyCCFARandomResidual(sEval, m, "s");
    ApplyCCFARandomResidual(cEval, m, "cu");

    Ciphertext<DCRTPoly> cu, qu, su;

    {
        const double startMs = nowMs();
        // Evaluate q and s2 at u.
        // If their degrees are larger than k, then recursively apply the Paterson-Stockmeyer algorithm.
        if (Degree(qEval) > k) {
            qu = InnerEvalChebyshevPS(x, qEval, k, m - 1, T, T2);
        }
        else {
            // dq = k from construction
            // perform scalar multiplication for all other terms and sum them up if there are non-zero coefficients

            // the highest order coefficient will always be a power of two up to 2^{m-1} because q is "monic" but the Chebyshev rule adds a factor of 2
            // we don't need to increase the depth by multiplying the highest order coefficient, but instead checking and summing, since we work with m <= 4.
            qu = T[k - 1]->Clone();
            const auto leading = ToReal(qEval.back());
            const uint32_t limit =
                (leading > 1.0) ? static_cast<uint32_t>(std::log2(leading) + 1e-12) : 0U;
            for (uint32_t i = 0; i < limit; ++i)
                cc->EvalAddInPlaceNoCheck(qu, qu);

            // adds the free term (at x^0)
            cc->EvalAddInPlace(qu, qEval.front() / 2.0);
            // The number of levels of qu is the same as the number of levels of T[k-1] + 1.
            // Will only get here when m = 2, so the number of levels of qu and T2[m-1] will be the same.

            qEval.resize(k);
            if (uint32_t n = Degree(qEval); n > 0)
                cc->EvalAddInPlace(qu, EvalPartialLinearWSum(T, qEval, n));
        }
        AppendCCFAProfileRow("ps", "qu", m, nowMs() - startMs, qu ? static_cast<int>(qu->GetLevel()) : -1, -1,
                             qu ? static_cast<int>(qu->GetElements().size()) : -1, -1);
    }

    {
        const double startMs = nowMs();
        if (Degree(sEval) > k) {
            su = InnerEvalChebyshevPS(x, sEval, k, m - 1, T, T2);
        }
        else {
            // the highest order coefficient will always be 1 because s is monic.
            su = T[k - 1]->Clone();

            // ds = k from construction
            // perform scalar multiplication for all other terms and sum them up if there are non-zero coefficients
            sEval.resize(k);
            if (uint32_t n = Degree(sEval); n > 0)
                cc->EvalAddInPlace(su, EvalPartialLinearWSum(T, sEval, n));

            // adds the free term (at x^0)
            cc->EvalAddInPlace(su, sEval.front() / 2.0);

            // The number of levels of su is the same as the number of levels of T[k-1] or T[k-1] + 1. Need to reduce it to T2[m-1] + 1.
            cc->LevelReduceInPlace(su, nullptr);
        }
        AppendCCFAProfileRow("ps", "su", m, nowMs() - startMs, su ? static_cast<int>(su->GetLevel()) : -1, -1,
                             su ? static_cast<int>(su->GetElements().size()) : -1, -1);
    }

    if (uint32_t n = Degree(cEval); n >= 1) {
        const double startMs = nowMs();
        if (n == 1) {
            if (IsNotEqualOne(cEval[1])) {
                cu = cc->EvalMult(T.front(), cEval[1]);
                cc->ModReduceInPlace(cu);
            }
            else {
                cu = T.front()->Clone();
            }
        }
        else {
            cu = EvalPartialLinearWSum(T, cEval, n);
        }

        // adds the free term (at x^0)
        cc->EvalAddInPlace(cu, cEval.front() / 2.0);

        // Need to reduce levels up to the level of T2[m-1].
        uint32_t cd =
            std::dynamic_pointer_cast<CryptoParametersCKKSRNS>(x->GetCryptoParameters())->GetCompositeDegree();
        cc->LevelReduceInPlace(cu, nullptr, (T2[m - 1]->GetLevel() - cu->GetLevel()) / cd);
        AppendCCFAProfileRow("ps", "cu_aligned", m, nowMs() - startMs, static_cast<int>(cu->GetLevel()),
                             static_cast<int>(T2[m - 1]->GetLevel()), static_cast<int>(cu->GetElements().size()),
                             static_cast<int>(T2[m - 1]->GetElements().size()));
    }

    {
        const double startMs = nowMs();
    cu = cu ? cc->EvalAdd(T2[m - 1], cu) : cc->EvalAdd(T2[m - 1], cEval.front() / 2.0);
        AppendCCFAProfileRow("ps", "combine_cu_t2", m, nowMs() - startMs, static_cast<int>(cu->GetLevel()),
                             static_cast<int>(qu->GetLevel()), static_cast<int>(cu->GetElements().size()),
                             static_cast<int>(qu->GetElements().size()));
    }

    const double multStartMs = nowMs();
    auto result = cc->EvalMult(cu, qu);
    AppendCCFAProfileRow("ps", "final_mult", m, nowMs() - multStartMs, static_cast<int>(cu->GetLevel()),
                         static_cast<int>(qu->GetLevel()), static_cast<int>(cu->GetElements().size()),
                         static_cast<int>(qu->GetElements().size()));
    const double modStartMs = nowMs();
    cc->ModReduceInPlace(result);
    AppendCCFAProfileRow("ps", "final_modreduce", m, nowMs() - modStartMs, static_cast<int>(result->GetLevel()),
                         static_cast<int>(su->GetLevel()), static_cast<int>(result->GetElements().size()),
                         static_cast<int>(su->GetElements().size()));
    const double addStartMs = nowMs();
    cc->EvalAddInPlace(result, su);
    AppendCCFAProfileRow("ps", "final_add_su", m, nowMs() - addStartMs, static_cast<int>(result->GetLevel()),
                         static_cast<int>(su->GetLevel()), static_cast<int>(result->GetElements().size()),
                         static_cast<int>(su->GetElements().size()));
    return result;
}

std::shared_ptr<seriesPowers<DCRTPoly>> internalEvalChebyPolysPS(ConstCiphertext<DCRTPoly>& x, uint32_t degree,
                                                                 double a, double b) {
    const auto nowMs = []() {
        return std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now().time_since_epoch())
            .count();
    };
    auto degs  = ComputeDegreesPS(degree);
    uint32_t k = degs[0];
    uint32_t m = degs[1];

    // computes linear transformation y = -1 + 2 (x-a)/(b-a)
    // consumes one level when a <> -1 && b <> 1
    auto cc = x->GetCryptoContext();
    std::vector<Ciphertext<DCRTPoly>> T(k);
    if (!IsNotEqualNegOne(a) && !IsNotEqualOne(b)) {
        // no linear transformation is needed if a = -1, b = 1
        // T_1(y) = y
        T[0] = x->Clone();
    }
    else {
        // linear transformation is needed
        double alpha = 2 / (b - a);
        double beta  = a * alpha;

        T[0] = cc->EvalMult(x, alpha);
        cc->ModReduceInPlace(T[0]);
        cc->EvalAddInPlace(T[0], -1.0 - beta);
    }

    // Computes Chebyshev polynomials up to degree k
    // for y: T_1(y) = y, T_2(y), ... , T_k(y)
    // uses binary tree multiplication
    for (uint32_t i = 2; i <= k; ++i) {
        const double stepStartMs = nowMs();
        if (i & 0x1) {  // if i is odd
            // compute T_{2i+1}(y) = 2*T_i(y)*T_{i+1}(y) - y
            T[i - 1] = cc->EvalMult(T[i / 2 - 1], T[i / 2]);
            cc->EvalAddInPlaceNoCheck(T[i - 1], T[i - 1]);
            cc->ModReduceInPlace(T[i - 1]);
            cc->EvalSubInPlace(T[i - 1], T[0]);
        }
        else {
            // compute T_{2i}(y) = 2*T_i(y)^2 - 1
            T[i - 1] = cc->EvalSquare(T[i / 2 - 1]);
            cc->EvalAddInPlaceNoCheck(T[i - 1], T[i - 1]);
            cc->ModReduceInPlace(T[i - 1]);
            cc->EvalAddInPlace(T[i - 1], -1.0);
        }
        AppendCCFAProfileRow("powers", "T_tree_step", i, nowMs() - stepStartMs, static_cast<int>(T[i - 1]->GetLevel()),
                             -1, static_cast<int>(T[i - 1]->GetElements().size()), -1);
    }

    const auto cryptoParams = std::dynamic_pointer_cast<CryptoParametersCKKSRNS>(T[k - 1]->GetCryptoParameters());
    if (cryptoParams->GetScalingTechnique() == FIXEDMANUAL) {
        // brings all powers of x to the same level
        for (uint32_t i = 1; i < k; ++i)
            cc->LevelReduceInPlace(T[i - 1], nullptr, T[k - 1]->GetLevel() - T[i - 1]->GetLevel());
    }
    else {
        for (uint32_t i = 1; i < k; ++i)
            cc->GetScheme()->AdjustLevelsAndDepthInPlace(T[i - 1], T[k - 1]);
    }

    std::vector<Ciphertext<DCRTPoly>> T2(m);
    // T2[0] is used as a placeholder
    T2[0] = T.back();

    // computes T_{k(2*m - 1)}(y)
    auto T2km1 = T.back();

    for (uint32_t i = 1; i < m; ++i) {
        const double squareStartMs = nowMs();
        // Compute the Chebyshev polynomials T_k(y), T_{2k}(y), T_{4k}(y), ... , T_{2^{m-1}k}(y)
        T2[i] = cc->EvalSquare(T2[i - 1]);
        cc->EvalAddInPlaceNoCheck(T2[i], T2[i]);
        cc->ModReduceInPlace(T2[i]);
        cc->EvalAddInPlace(T2[i], -1.0);
        AppendCCFAProfileRow("powers", "T2_square_step", i, nowMs() - squareStartMs, static_cast<int>(T2[i]->GetLevel()),
                             -1, static_cast<int>(T2[i]->GetElements().size()), -1);

        // compute T_{k(2*m - 1)} = 2*T_{k(2^{m-1}-1)}(y)*T_{k*2^{m-1}}(y) - T_k(y)
        const double chainStartMs = nowMs();
        T2km1 = cc->EvalMult(T2km1, T2[i]);
        cc->EvalAddInPlaceNoCheck(T2km1, T2km1);
        cc->ModReduceInPlace(T2km1);
        cc->EvalSubInPlace(T2km1, T2[0]);
        AppendCCFAProfileRow("powers", "T2km1_chain_step", i, nowMs() - chainStartMs,
                             static_cast<int>(T2km1->GetLevel()), -1, static_cast<int>(T2km1->GetElements().size()),
                             -1);
    }

    return std::make_shared<seriesPowers<DCRTPoly>>(std::move(T), std::move(T2), std::move(T2km1), k, m);
}

template <typename VectorDataType>
Ciphertext<DCRTPoly> internalEvalChebyshevSeriesPSWithPrecomp(const std::shared_ptr<seriesPowers<DCRTPoly>>& ctxtPolys,
                                                              const std::vector<VectorDataType>& coefficients) {
    auto& T     = ctxtPolys->powersRe;
    auto& T2    = ctxtPolys->powers2Re;
    auto& T2km1 = ctxtPolys->power2km1Re;
    auto k      = ctxtPolys->k;
    auto m      = ctxtPolys->m;

    // Compute k*2^{m-1}-k because we use it a lot
    uint32_t k2m2k = k * (1 << (m - 1)) - k;

    // Add T^{k(2^m - 1)}(y) to the polynomial that has to be evaluated
    auto f2 = coefficients;
    f2.resize(Degree(f2) + 1);
    f2.resize(2 * k2m2k + k + 1);
    f2.back() = 1;

    return T[0]->GetCryptoContext()->EvalSub(InnerEvalChebyshevPS(T[0], f2, k, m, T, T2), T2km1);
}

std::shared_ptr<seriesPowers<DCRTPoly>> AdvancedSHECKKSRNS::EvalChebyPolys(ConstCiphertext<DCRTPoly>& x,
                                                                           const std::vector<int64_t>& coefficients,
                                                                           double a, double b) const {
    uint32_t d = Degree(coefficients);
    return (d < 5) ? internalEvalChebyPolysLinear(x, coefficients, a, b) : internalEvalChebyPolysPS(x, d, a, b);
}
std::shared_ptr<seriesPowers<DCRTPoly>> AdvancedSHECKKSRNS::EvalChebyPolys(ConstCiphertext<DCRTPoly>& x,
                                                                           const std::vector<double>& coefficients,
                                                                           double a, double b) const {
    uint32_t d = Degree(coefficients);
    return (d < 5) ? internalEvalChebyPolysLinear(x, coefficients, a, b) : internalEvalChebyPolysPS(x, d, a, b);
}
std::shared_ptr<seriesPowers<DCRTPoly>> AdvancedSHECKKSRNS::EvalChebyPolys(
    ConstCiphertext<DCRTPoly>& x, const std::vector<std::complex<double>>& coefficients, double a, double b) const {
    uint32_t d = Degree(coefficients);
    return (d < 5) ? internalEvalChebyPolysLinear(x, coefficients, a, b) : internalEvalChebyPolysPS(x, d, a, b);
}

Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalChebyshevSeries(ConstCiphertext<DCRTPoly>& x,
                                                             const std::vector<int64_t>& coeffs, double a,
                                                             double b) const {
    return (Degree(coeffs) < 5) ? EvalChebyshevSeriesLinear(x, coeffs, a, b) : EvalChebyshevSeriesPS(x, coeffs, a, b);
}
Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalChebyshevSeries(ConstCiphertext<DCRTPoly>& x,
                                                             const std::vector<double>& coeffs, double a,
                                                             double b) const {
    return (Degree(coeffs) < 5) ? EvalChebyshevSeriesLinear(x, coeffs, a, b) : EvalChebyshevSeriesPS(x, coeffs, a, b);
}
Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalChebyshevSeries(ConstCiphertext<DCRTPoly>& x,
                                                             const std::vector<std::complex<double>>& coeffs, double a,
                                                             double b) const {
    return (Degree(coeffs) < 5) ? EvalChebyshevSeriesLinear(x, coeffs, a, b) : EvalChebyshevSeriesPS(x, coeffs, a, b);
}

Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalChebyshevSeriesWithPrecomp(
    std::shared_ptr<seriesPowers<DCRTPoly>> ctxtPowers, const std::vector<int64_t>& coeffs) const {
    return (Degree(coeffs) < 5) ? internalEvalChebyshevSeriesLinearWithPrecomp(ctxtPowers->powersRe, coeffs) :
                                  internalEvalChebyshevSeriesPSWithPrecomp(ctxtPowers, coeffs);
}
Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalChebyshevSeriesWithPrecomp(
    std::shared_ptr<seriesPowers<DCRTPoly>> ctxtPowers, const std::vector<double>& coeffs) const {
    return (Degree(coeffs) < 5) ? internalEvalChebyshevSeriesLinearWithPrecomp(ctxtPowers->powersRe, coeffs) :
                                  internalEvalChebyshevSeriesPSWithPrecomp(ctxtPowers, coeffs);
}
Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalChebyshevSeriesWithPrecomp(
    std::shared_ptr<seriesPowers<DCRTPoly>> ctxtPowers, const std::vector<std::complex<double>>& coeffs) const {
    return (Degree(coeffs) < 5) ? internalEvalChebyshevSeriesLinearWithPrecomp(ctxtPowers->powersRe, coeffs) :
                                  internalEvalChebyshevSeriesPSWithPrecomp(ctxtPowers, coeffs);
}

Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalChebyshevSeriesLinear(ConstCiphertext<DCRTPoly>& x,
                                                                   const std::vector<int64_t>& coeffs, double a,
                                                                   double b) const {
    return internalEvalChebyshevSeriesLinearWithPrecomp(internalEvalChebyPolysLinear(x, coeffs, a, b)->powersRe,
                                                        coeffs);
}
Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalChebyshevSeriesLinear(ConstCiphertext<DCRTPoly>& x,
                                                                   const std::vector<double>& coeffs, double a,
                                                                   double b) const {
    return internalEvalChebyshevSeriesLinearWithPrecomp(internalEvalChebyPolysLinear(x, coeffs, a, b)->powersRe,
                                                        coeffs);
}
Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalChebyshevSeriesLinear(ConstCiphertext<DCRTPoly>& x,
                                                                   const std::vector<std::complex<double>>& coeffs,
                                                                   double a, double b) const {
    return internalEvalChebyshevSeriesLinearWithPrecomp(internalEvalChebyPolysLinear(x, coeffs, a, b)->powersRe,
                                                        coeffs);
}

Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalChebyshevSeriesPS(ConstCiphertext<DCRTPoly>& x,
                                                               const std::vector<int64_t>& coeffs, double a,
                                                               double b) const {
    return internalEvalChebyshevSeriesPSWithPrecomp(internalEvalChebyPolysPS(x, Degree(coeffs), a, b), coeffs);
}
Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalChebyshevSeriesPS(ConstCiphertext<DCRTPoly>& x,
                                                               const std::vector<double>& coeffs, double a,
                                                               double b) const {
    return internalEvalChebyshevSeriesPSWithPrecomp(internalEvalChebyPolysPS(x, Degree(coeffs), a, b), coeffs);
}
Ciphertext<DCRTPoly> AdvancedSHECKKSRNS::EvalChebyshevSeriesPS(ConstCiphertext<DCRTPoly>& x,
                                                               const std::vector<std::complex<double>>& coeffs,
                                                               double a, double b) const {
    return internalEvalChebyshevSeriesPSWithPrecomp(internalEvalChebyPolysPS(x, Degree(coeffs), a, b), coeffs);
}

}  // namespace lbcrypto
