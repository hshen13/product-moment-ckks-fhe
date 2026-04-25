#include "openfhe.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
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

std::vector<std::vector<double>> ReadCsv(const std::string& path, uint32_t maxRows = 0) {
    std::ifstream in(path);
    if (!in) {
        OPENFHE_THROW("Cannot open CSV: " + path);
    }
    std::vector<std::vector<double>> rows;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        std::vector<double> row;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, ',')) {
            row.push_back(std::stod(cell));
        }
        rows.push_back(std::move(row));
        if (maxRows != 0 && rows.size() >= maxRows) {
            break;
        }
    }
    return rows;
}

double Pearson(const std::vector<double>& a, const std::vector<double>& b) {
    const size_t n = a.size();
    double ma = std::accumulate(a.begin(), a.end(), 0.0) / static_cast<double>(n);
    double mb = std::accumulate(b.begin(), b.end(), 0.0) / static_cast<double>(n);
    double num = 0.0;
    double va = 0.0;
    double vb = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double da = a[i] - ma;
        const double db = b[i] - mb;
        num += da * db;
        va += da * da;
        vb += db * db;
    }
    const double den = std::sqrt(va * vb);
    return den > 0.0 ? num / den : 0.0;
}

double MeanAbs(const std::vector<double>& a, const std::vector<double>& b) {
    double total = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        total += std::abs(a[i] - b[i]);
    }
    return total / static_cast<double>(a.size());
}

double MeanSigned(const std::vector<double>& a, const std::vector<double>& b) {
    double total = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        total += a[i] - b[i];
    }
    return total / static_cast<double>(a.size());
}

std::vector<double> PlainPrs(const std::vector<std::vector<double>>& geno, const std::vector<double>& beta) {
    std::vector<double> out(geno.size(), 0.0);
    for (size_t sample = 0; sample < geno.size(); ++sample) {
        for (size_t snp = 0; snp < beta.size(); ++snp) {
            out[sample] += beta[snp] * geno[sample][snp];
        }
    }
    return out;
}

std::vector<uint32_t> TopK(const std::vector<double>& values, uint32_t k) {
    std::vector<uint32_t> idx(values.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::partial_sort(idx.begin(), idx.begin() + std::min<size_t>(k, idx.size()), idx.end(),
                      [&](uint32_t a, uint32_t b) { return values[a] > values[b]; });
    idx.resize(std::min<size_t>(k, idx.size()));
    std::sort(idx.begin(), idx.end());
    return idx;
}

double TopKAgreement(const std::vector<double>& a, const std::vector<double>& b, uint32_t k) {
    const auto ta = TopK(a, k);
    const auto tb = TopK(b, k);
    uint32_t same = 0;
    for (uint32_t x : ta) {
        if (std::binary_search(tb.begin(), tb.end(), x)) {
            ++same;
        }
    }
    return k == 0 ? 1.0 : static_cast<double>(same) / static_cast<double>(k);
}

struct Runtime {
    CryptoContext<DCRTPoly> cc;
    KeyPair<DCRTPoly> kp;
    uint32_t slots = 0;
};

Runtime Setup(uint32_t ringDim, uint32_t slots) {
    Runtime rt;
    rt.slots = slots;
    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(ringDim);
    parameters.SetBatchSize(slots);
    parameters.SetScalingTechnique(FLEXIBLEAUTO);
    parameters.SetScalingModSize(ReadEnvUInt("OPENFHE_CCFA_HEPRS_DCRT_BITS", 50));
    parameters.SetFirstModSize(ReadEnvUInt("OPENFHE_CCFA_HEPRS_FIRST_MOD", 60));
    parameters.SetMultiplicativeDepth(3);

    rt.cc = GenCryptoContext(parameters);
    rt.cc->Enable(PKE);
    rt.cc->Enable(KEYSWITCH);
    rt.cc->Enable(LEVELEDSHE);
    rt.cc->Enable(ADVANCEDSHE);
    rt.kp = rt.cc->KeyGen();
    rt.cc->EvalMultKeyGen(rt.kp.secretKey);
    rt.cc->EvalSumKeyGen(rt.kp.secretKey);
    return rt;
}

std::vector<double> BlockVector(const std::vector<double>& row, uint32_t block, uint32_t blockSize, double scale) {
    std::vector<double> out(blockSize, 0.0);
    const size_t begin = static_cast<size_t>(block) * blockSize;
    for (uint32_t i = 0; i < blockSize && begin + i < row.size(); ++i) {
        out[i] = row[begin + i] * scale;
    }
    return out;
}

Ciphertext<DCRTPoly> EncryptVector(const Runtime& rt, const std::vector<double>& values) {
    auto pt = rt.cc->MakeCKKSPackedPlaintext(values, 1, 2, nullptr, rt.slots);
    pt->SetLength(values.size());
    return rt.cc->Encrypt(rt.kp.publicKey, pt);
}

double DecryptFirst(const Runtime& rt, ConstCiphertext<DCRTPoly>& ct) {
    Plaintext dec;
    rt.cc->Decrypt(rt.kp.secretKey, ct, &dec);
    dec->SetLength(1);
    return dec->GetCKKSPackedValue()[0].real();
}

struct EvalResult {
    std::vector<double> prs;
    double latencyMs = 0.0;
    uint32_t activeBlocks = 0;
    uint32_t skippedBlocks = 0;
};

EvalResult EvaluatePrs(const Runtime& rt, const std::vector<std::vector<double>>& geno, const std::vector<double>& beta,
                       uint32_t blockSize, const std::vector<uint8_t>& keep, const std::vector<double>& blockScale) {
    EvalResult result;
    result.prs.resize(geno.size(), 0.0);
    const uint32_t blocks = static_cast<uint32_t>((beta.size() + blockSize - 1) / blockSize);
    std::vector<Ciphertext<DCRTPoly>> betaCt(blocks);
    std::vector<std::vector<Ciphertext<DCRTPoly>>> genoCt(geno.size(), std::vector<Ciphertext<DCRTPoly>>(blocks));
    for (uint32_t block = 0; block < blocks; ++block) {
        if (!keep[block]) {
            ++result.skippedBlocks;
            continue;
        }
        ++result.activeBlocks;
        betaCt[block] = EncryptVector(rt, BlockVector(beta, block, blockSize, blockScale[block]));
        for (size_t sample = 0; sample < geno.size(); ++sample) {
            genoCt[sample][block] = EncryptVector(rt, BlockVector(geno[sample], block, blockSize, blockScale[block]));
        }
    }

    const auto start = std::chrono::steady_clock::now();
    std::vector<Ciphertext<DCRTPoly>> prsCt(geno.size());
    for (size_t sample = 0; sample < geno.size(); ++sample) {
        Ciphertext<DCRTPoly> acc;
        for (uint32_t block = 0; block < blocks; ++block) {
            if (!keep[block]) {
                continue;
            }
            auto blockDot = rt.cc->EvalInnerProduct(betaCt[block], genoCt[sample][block], blockSize);
            if (!acc) {
                acc = blockDot;
            }
            else {
                rt.cc->EvalAddInPlace(acc, blockDot);
            }
        }
        prsCt[sample] = acc;
    }
    const auto stop = std::chrono::steady_clock::now();
    result.latencyMs = std::chrono::duration<double, std::milli>(stop - start).count();
    for (size_t sample = 0; sample < geno.size(); ++sample) {
        result.prs[sample] = DecryptFirst(rt, prsCt[sample]);
    }
    return result;
}

}  // namespace

int main() {
    const std::string genoPath = ReadEnvString(
        "OPENFHE_CCFA_HEPRS_GENOTYPE",
        "/workspace/openfhe_ccfa_githubready/third_party/HEPRS/example_data/genotype_10kSNP_50individual.csv");
    const std::string betaPath = ReadEnvString(
        "OPENFHE_CCFA_HEPRS_BETA",
        "/workspace/openfhe_ccfa_githubready/third_party/HEPRS/example_data/beta_10kSNP_phenotype0.csv");
    const std::string outCsv =
        ReadEnvString("OPENFHE_CCFA_HEPRS_OUTPUT", "/workspace/results/ccs2026_heprs/heprs_prs_block_skip.csv");
    const uint32_t samples = ReadEnvUInt("OPENFHE_CCFA_HEPRS_SAMPLES", 20);
    const uint32_t blockSize = ReadEnvUInt("OPENFHE_CCFA_HEPRS_BLOCK_SIZE", 512);
    const uint32_t ringDim = ReadEnvUInt("OPENFHE_CCFA_HEPRS_RING_DIM", 1u << 12);
    const uint32_t trials = ReadEnvUInt("OPENFHE_CCFA_HEPRS_TRIALS", 30);
    const uint32_t firstSeed = ReadEnvUInt("OPENFHE_CCFA_HEPRS_FIRST_SEED", 1);
    const double keepProb = ReadEnvDouble("OPENFHE_CCFA_HEPRS_KEEP_PROB", 0.90);
    const uint32_t protectBlocks = ReadEnvUInt("OPENFHE_CCFA_HEPRS_PROTECT_BLOCKS", 0);
    const double scaleExp = ReadEnvDouble("OPENFHE_CCFA_HEPRS_SCALE_EXP", 0.5);
    const std::string onlyMode = ReadEnvString("OPENFHE_CCFA_HEPRS_ONLY", "");
    const std::string predOut = ReadEnvString("OPENFHE_CCFA_HEPRS_PRED_OUT", "");

    auto geno = ReadCsv(genoPath, samples);
    auto betaRows = ReadCsv(betaPath, 1);
    if (geno.empty() || betaRows.empty()) {
        OPENFHE_THROW("HEPRS input CSVs are empty");
    }
    auto beta = betaRows.front();
    const size_t snps = std::min(beta.size(), geno.front().size());
    beta.resize(snps);
    for (auto& row : geno) {
        row.resize(snps);
    }
    const uint32_t blocks = static_cast<uint32_t>((snps + blockSize - 1) / blockSize);
    const uint32_t topK = std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(geno.size() * 0.10)));
    std::vector<std::pair<double, uint32_t>> blockEnergy;
    blockEnergy.reserve(blocks);
    for (uint32_t block = 0; block < blocks; ++block) {
        double energy = 0.0;
        const size_t begin = static_cast<size_t>(block) * blockSize;
        for (uint32_t i = 0; i < blockSize && begin + i < beta.size(); ++i) {
            energy += std::abs(beta[begin + i]);
        }
        blockEnergy.push_back({energy, block});
    }
    std::sort(blockEnergy.begin(), blockEnergy.end(), std::greater<>());
    std::vector<uint8_t> protectedBlock(blocks, 0);
    for (uint32_t i = 0; i < std::min<uint32_t>(protectBlocks, blocks); ++i) {
        protectedBlock[blockEnergy[i].second] = 1;
    }

    std::filesystem::create_directories(std::filesystem::path(outCsv).parent_path());
    Runtime rt = Setup(ringDim, blockSize);
    const auto plain = PlainPrs(geno, beta);

    std::ofstream out(outCsv);
    out << "mode,trial,seed,samples,snps,block_size,blocks,active_blocks,skipped_blocks,latency_ms,"
           "protected_blocks,speedup_vs_deterministic_pct,mae_vs_det,mean_signed_vs_det,pearson_vs_det,top_decile_agreement,"
           "mae_vs_plain,pearson_vs_plain,keep_prob\n";
    out << std::scientific << std::setprecision(12);

    std::vector<uint8_t> allKeep(blocks, 1);
    std::vector<double> allScale(blocks, 1.0);
    auto det = EvaluatePrs(rt, geno, beta, blockSize, allKeep, allScale);
    const double detPlainMae = MeanAbs(det.prs, plain);
    const double detPlainPearson = Pearson(det.prs, plain);
    out << "deterministic,0,0," << geno.size() << ',' << snps << ',' << blockSize << ',' << blocks << ','
        << det.activeBlocks << ',' << det.skippedBlocks << ',' << det.latencyMs << ',' << protectBlocks
        << ",0,0,0,1,1," << detPlainMae
        << ',' << detPlainPearson << ',' << keepProb << '\n';

    for (uint32_t trial = 0; trial < trials; ++trial) {
        const uint32_t seed = firstSeed + trial;
        std::mt19937 rng(seed);
        std::bernoulli_distribution keepDist(keepProb);
        std::vector<uint8_t> keep(blocks, 0);
        bool any = false;
        for (uint32_t block = 0; block < blocks; ++block) {
            keep[block] = (protectedBlock[block] || keepDist(rng)) ? 1 : 0;
            any = any || keep[block];
        }
        if (!any) {
            keep[static_cast<uint32_t>(seed % blocks)] = 1;
        }

        std::vector<double> unscaled(blocks, 1.0);
        std::vector<double> productScale(blocks, 1.0);
        for (uint32_t block = 0; block < blocks; ++block) {
            if (!protectedBlock[block]) {
                productScale[block] = std::pow(keepProb, -scaleExp);
            }
        }
        for (const auto& item : {std::make_pair("unscaled_skip", unscaled),
                                 std::make_pair("product_coupled", productScale)}) {
            if (!onlyMode.empty() && onlyMode != item.first) {
                continue;
            }
            auto row = EvaluatePrs(rt, geno, beta, blockSize, keep, item.second);
            const double speedup = 100.0 * (det.latencyMs - row.latencyMs) / det.latencyMs;
            out << item.first << ',' << trial << ',' << seed << ',' << geno.size() << ',' << snps << ',' << blockSize
                << ',' << blocks << ',' << row.activeBlocks << ',' << row.skippedBlocks << ',' << row.latencyMs << ','
                << protectBlocks << ',' << speedup << ',' << MeanAbs(row.prs, det.prs) << ',' << MeanSigned(row.prs, det.prs) << ','
                << Pearson(row.prs, det.prs) << ',' << TopKAgreement(row.prs, det.prs, topK) << ','
                << MeanAbs(row.prs, plain) << ',' << Pearson(row.prs, plain) << ',' << keepProb << '\n';
            out.flush();
            if (!predOut.empty()) {
                const bool exists = std::filesystem::exists(predOut);
                std::ofstream pred(predOut, std::ios::app);
                if (!exists) {
                    pred << "mode,trial,seed,sample,det_prs,randomized_prs,plain_prs,error_vs_det\n";
                    pred << std::scientific << std::setprecision(12);
                }
                for (size_t sample = 0; sample < row.prs.size(); ++sample) {
                    pred << item.first << ',' << trial << ',' << seed << ',' << sample << ',' << det.prs[sample] << ','
                         << row.prs[sample] << ',' << plain[sample] << ',' << (row.prs[sample] - det.prs[sample])
                         << '\n';
                }
            }
        }
    }

    std::cout << "Wrote HEPRS PRS block-skip results to " << outCsv << std::endl;
    return 0;
}
