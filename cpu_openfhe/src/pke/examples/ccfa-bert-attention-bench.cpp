#include "openfhe.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
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

void ClearCcfaEnv() {
    setenv("OPENFHE_CCFA_MODE", "none", 1);
    unsetenv("OPENFHE_CCFA_DIST");
    unsetenv("OPENFHE_CCFA_COUPLING");
    unsetenv("OPENFHE_CCFA_KEEP_PROB");
    unsetenv("OPENFHE_CCFA_MIN_SCALE");
    unsetenv("OPENFHE_CCFA_SEED");
    unsetenv("OPENFHE_CCFA_MIN_M");
    unsetenv("OPENFHE_CCFA_MAX_M");
}

void SetCcfaEnv(const std::string& mode, double keepProb, uint32_t seed) {
    if (mode == "deterministic") {
        ClearCcfaEnv();
        return;
    }
    setenv("OPENFHE_CCFA_MODE", mode == "product_coupled" ? "product" : "independent", 1);
    setenv("OPENFHE_CCFA_DIST", "bernoulli", 1);
    setenv("OPENFHE_CCFA_COUPLING", mode == "product_coupled" ? "product_coupled" : "independent", 1);
    setenv("OPENFHE_CCFA_KEEP_PROB", std::to_string(keepProb).c_str(), 1);
    setenv("OPENFHE_CCFA_MIN_SCALE", std::to_string(ReadEnvDouble("OPENFHE_CCFA_E9_MIN_SCALE", 0.0)).c_str(), 1);
    setenv("OPENFHE_CCFA_SEED", std::to_string(seed).c_str(), 1);
    setenv("OPENFHE_CCFA_MIN_M", "2", 1);
    setenv("OPENFHE_CCFA_MAX_M", "16", 1);
}

std::vector<int32_t> SumRotations(uint32_t slots) {
    std::vector<int32_t> rotations;
    for (uint32_t step = 1; step < slots; step <<= 1) {
        rotations.push_back(static_cast<int32_t>(step));
    }
    return rotations;
}

std::vector<double> RandomVector(uint32_t dim, uint32_t slots, uint32_t seed, double scale) {
    std::mt19937 gen(seed);
    std::normal_distribution<double> dist(0.0, scale);
    std::vector<double> out(slots, 0.0);
    for (uint32_t i = 0; i < dim; ++i) {
        out[i] = std::max(-0.95, std::min(0.95, dist(gen)));
    }
    return out;
}

std::vector<double> AddNoise(const std::vector<double>& base, uint32_t dim, uint32_t seed, double scale) {
    std::mt19937 gen(seed);
    std::normal_distribution<double> dist(0.0, scale);
    std::vector<double> out = base;
    for (uint32_t i = 0; i < dim; ++i) {
        out[i] = std::max(-0.95, std::min(0.95, base[i] + dist(gen)));
    }
    return out;
}

std::vector<double> DecryptReal(CryptoContext<DCRTPoly> cc, const PrivateKey<DCRTPoly>& sk,
                                ConstCiphertext<DCRTPoly>& ct, uint32_t length) {
    Plaintext dec;
    cc->Decrypt(sk, ct, &dec);
    dec->SetLength(length);
    std::vector<double> out;
    out.reserve(length);
    for (const auto& value : dec->GetCKKSPackedValue()) {
        out.push_back(value.real());
    }
    return out;
}

Ciphertext<DCRTPoly> RotateSum(CryptoContext<DCRTPoly> cc, Ciphertext<DCRTPoly> ct, uint32_t slots) {
    for (uint32_t step = 1; step < slots; step <<= 1) {
        ct = cc->EvalAdd(ct, cc->EvalRotate(ct, static_cast<int32_t>(step)));
    }
    return ct;
}

std::vector<double> Softmax(const std::vector<double>& row) {
    const double maxVal = *std::max_element(row.begin(), row.end());
    std::vector<double> exps(row.size());
    double sum = 0.0;
    for (size_t i = 0; i < row.size(); ++i) {
        exps[i] = std::exp(row[i] - maxVal);
        sum += exps[i];
    }
    for (double& v : exps) {
        v /= sum;
    }
    return exps;
}

uint32_t Argmax(const std::vector<double>& row) {
    return static_cast<uint32_t>(std::distance(row.begin(), std::max_element(row.begin(), row.end())));
}

struct Runtime {
    CryptoContext<DCRTPoly> cc;
    KeyPair<DCRTPoly> kp;
    Plaintext mask;
    uint32_t slots = 0;
};

Runtime Setup(uint32_t ringDim, uint32_t slots, uint32_t depth, uint32_t dim) {
    Runtime rt;
    rt.slots = slots;
    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(ringDim);
    parameters.SetBatchSize(slots);
    parameters.SetScalingTechnique(FLEXIBLEAUTO);
    parameters.SetScalingModSize(ReadEnvUInt("OPENFHE_CCFA_E9_DCRT_BITS", 50));
    parameters.SetFirstModSize(ReadEnvUInt("OPENFHE_CCFA_E9_FIRST_MOD", 60));
    parameters.SetMultiplicativeDepth(depth);
    rt.cc = GenCryptoContext(parameters);
    rt.cc->Enable(PKE);
    rt.cc->Enable(KEYSWITCH);
    rt.cc->Enable(LEVELEDSHE);
    rt.cc->Enable(ADVANCEDSHE);
    rt.kp = rt.cc->KeyGen();
    rt.cc->EvalMultKeyGen(rt.kp.secretKey);
    rt.cc->EvalRotateKeyGen(rt.kp.secretKey, SumRotations(slots));

    std::vector<double> mask(slots, 0.0);
    for (uint32_t i = 0; i < dim; ++i) {
        mask[i] = 1.0;
    }
    rt.mask = rt.cc->MakeCKKSPackedPlaintext(mask, 1, 0, nullptr, slots);
    return rt;
}

std::vector<Ciphertext<DCRTPoly>> Activate(const Runtime& rt, const std::vector<std::vector<double>>& inputs,
                                           uint32_t degree) {
    std::vector<Ciphertext<DCRTPoly>> out;
    out.reserve(inputs.size());
    for (const auto& values : inputs) {
        auto pt = rt.cc->MakeCKKSPackedPlaintext(values, 1, 0, nullptr, rt.slots);
        auto ct = rt.cc->Encrypt(rt.kp.publicKey, pt);
        out.push_back(
            rt.cc->EvalChebyshevFunction([](double v) { return v < 0.0 ? 0.0 : v; }, ct, -1.0, 1.0, degree));
    }
    return out;
}

std::vector<std::vector<double>> Scores(const Runtime& rt, const std::vector<Ciphertext<DCRTPoly>>& q,
                                        const std::vector<Ciphertext<DCRTPoly>>& k, uint32_t slots, double scale) {
    std::vector<std::vector<double>> scores(q.size(), std::vector<double>(k.size(), 0.0));
    for (size_t i = 0; i < q.size(); ++i) {
        for (size_t j = 0; j < k.size(); ++j) {
            auto prod = rt.cc->EvalMult(q[i], k[j]);
            prod = rt.cc->EvalMult(prod, rt.mask);
            auto sum = RotateSum(rt.cc, prod, slots);
            auto dec = DecryptReal(rt.cc, rt.kp.secretKey, sum, 1);
            scores[i][j] = (dec.empty() ? 0.0 : dec[0]) / scale;
        }
    }
    return scores;
}

}  // namespace

int main() {
    const uint32_t dim = ReadEnvUInt("OPENFHE_CCFA_E9_DIM", 768);
    const uint32_t tokens = ReadEnvUInt("OPENFHE_CCFA_E9_TOKENS", 8);
    const uint32_t seeds = ReadEnvUInt("OPENFHE_CCFA_E9_SEEDS", 5);
    const uint32_t firstSeed = ReadEnvUInt("OPENFHE_CCFA_E9_FIRST_SEED", 1);
    const uint32_t ringDim = ReadEnvUInt("OPENFHE_CCFA_E9_RING_DIM", 1u << 12);
    const uint32_t slots = ReadEnvUInt("OPENFHE_CCFA_E9_SLOTS", 1024);
    const uint32_t depth = ReadEnvUInt("OPENFHE_CCFA_E9_DEPTH", 12);
    const uint32_t degree = ReadEnvUInt("OPENFHE_CCFA_E9_DEGREE", 59);
    const double keepProb = ReadEnvDouble("OPENFHE_CCFA_E9_KEEP_PROB", 0.60);
    const double inputScale = ReadEnvDouble("OPENFHE_CCFA_E9_INPUT_SCALE", 0.35);
    const double keyNoise = ReadEnvDouble("OPENFHE_CCFA_E9_KEY_NOISE", 0.05);
    const bool correlatedKeys = ReadEnvUInt("OPENFHE_CCFA_E9_CORRELATED_KEYS", 1) != 0;
    const std::string outCsv =
        ReadEnvString("OPENFHE_CCFA_E9_OUTPUT", "/workspace/results/ccs2026_product/e9_bert_attention.csv");

    if (dim > slots) {
        OPENFHE_THROW("OPENFHE_CCFA_E9_DIM must be <= OPENFHE_CCFA_E9_SLOTS");
    }

    std::filesystem::create_directories(std::filesystem::path(outCsv).parent_path());
    Runtime rt = Setup(ringDim, slots, depth, dim);
    std::ofstream out(outCsv);
    out << "mode,seed,row,success,latency_ms,score_mae,score_max_abs,softmax_mae,top1_match,error,tokens,dim,slots,degree,keep_prob\n";
    out << std::scientific << std::setprecision(12);

    for (uint32_t s = 0; s < seeds; ++s) {
        const uint32_t seed = firstSeed + s;
        std::vector<std::vector<double>> q(tokens);
        std::vector<std::vector<double>> k(tokens);
        for (uint32_t t = 0; t < tokens; ++t) {
            q[t] = RandomVector(dim, slots, 101 * seed + 17 * t + 1, inputScale);
            k[t] = correlatedKeys ? AddNoise(q[t], dim, 211 * seed + 29 * t + 3, keyNoise) :
                                    RandomVector(dim, slots, 211 * seed + 29 * t + 3, inputScale);
        }

        ClearCcfaEnv();
        const auto detStart = std::chrono::steady_clock::now();
        auto detQ = Activate(rt, q, degree);
        auto detK = Activate(rt, k, degree);
        auto detScores = Scores(rt, detQ, detK, slots, std::sqrt(static_cast<double>(dim)));
        const auto detStop = std::chrono::steady_clock::now();
        const double detLatency = std::chrono::duration<double, std::milli>(detStop - detStart).count();
        for (uint32_t row = 0; row < tokens; ++row) {
            out << "deterministic," << seed << ',' << row << ",1," << detLatency << ",0,0,0,1,\"\","
                << tokens << ',' << dim << ',' << slots << ',' << degree << ',' << keepProb << '\n';
        }

        for (const std::string mode : {"independent", "product_coupled"}) {
            try {
                SetCcfaEnv(mode, keepProb, seed);
                const auto start = std::chrono::steady_clock::now();
                auto randQ = Activate(rt, q, degree);
                auto randK = Activate(rt, k, degree);
                auto randScores = Scores(rt, randQ, randK, slots, std::sqrt(static_cast<double>(dim)));
                const auto stop = std::chrono::steady_clock::now();
                const double latency = std::chrono::duration<double, std::milli>(stop - start).count();
                ClearCcfaEnv();

                for (uint32_t row = 0; row < tokens; ++row) {
                    double scoreMae = 0.0;
                    double scoreMax = 0.0;
                    for (uint32_t col = 0; col < tokens; ++col) {
                        const double err = std::abs(randScores[row][col] - detScores[row][col]);
                        scoreMae += err;
                        scoreMax = std::max(scoreMax, err);
                    }
                    scoreMae /= static_cast<double>(tokens);
                    auto detSoft = Softmax(detScores[row]);
                    auto randSoft = Softmax(randScores[row]);
                    double softMae = 0.0;
                    for (uint32_t col = 0; col < tokens; ++col) {
                        softMae += std::abs(randSoft[col] - detSoft[col]);
                    }
                    softMae /= static_cast<double>(tokens);
                    const bool top1 = Argmax(detSoft) == Argmax(randSoft);
                    out << mode << ',' << seed << ',' << row << ",1," << latency << ',' << scoreMae << ','
                        << scoreMax << ',' << softMae << ',' << (top1 ? 1 : 0) << ",\"\"," << tokens << ','
                        << dim << ',' << slots << ',' << degree << ',' << keepProb << '\n';
                }
            }
            catch (const std::exception& e) {
                ClearCcfaEnv();
                for (uint32_t row = 0; row < tokens; ++row) {
                    out << mode << ',' << seed << ',' << row << ",0,nan,nan,nan,nan,0,\"" << e.what() << "\","
                        << tokens << ',' << dim << ',' << slots << ',' << degree << ',' << keepProb << '\n';
                }
            }
        }
    }

    std::cout << "Wrote BERT-style attention results to " << outCsv << std::endl;
    return 0;
}
