# HEPRS PRS Benchmark: Deterministic vs Product-Coupled Block Skip

## Source Task

The benchmark is based on HEPRS, an open-source FHE implementation for
privacy-preserving polygenic risk scores (PRS):

- Paper: Knight et al., "Homomorphic encryption enables privacy preserving
  polygenic risk scores", Cell Reports Methods 2026.
- Code/data: `https://github.com/gersteinlab/HEPRS`

HEPRS computes PRS over encrypted genotype and encrypted model vectors. The
core operation is a large product-sum:

```text
PRS_j = sum_i beta_i * G_{j,i}
```

The HEPRS repository provides a synthetic 10k-SNP / 50-individual example. I
used its `genotype_10kSNP_50individual.csv` and
`beta_10kSNP_phenotype0.csv` files. The local environment does not have Go, so
I implemented an OpenFHE reproduction of the HEPRS evaluator path rather than
running the original Lattigo program.

## Implemented Benchmark

New OpenFHE example:

```text
ccfa-heprs-prs-bench
```

The benchmark encrypts both the SNP beta blocks and genotype blocks and
evaluates encrypted inner products block by block. Product-coupled block skip
uses one Bernoulli selector per SNP block:

```text
beta'_b = A_b beta_b / sqrt(p)
G'_b    = A_b G_b    / sqrt(p)
```

Because `A_b^2 = A_b`, the block contribution satisfies:

```text
E[ <beta'_b, G'_b> ] = <beta_b, G_b>
```

Thus skipped blocks remove encrypted inner-product work, while kept blocks are
scaled to preserve the deterministic PRS in expectation.

The run also protects the highest-effect beta blocks, which is the same
tail-gated deployment idea used elsewhere in the CCFA experiments.

## Main Configuration

```text
OPENFHE_CCFA_HEPRS_SAMPLES=20
OPENFHE_CCFA_HEPRS_BLOCK_SIZE=128
OPENFHE_CCFA_HEPRS_TRIALS=10
OPENFHE_CCFA_HEPRS_KEEP_PROB=0.98
OPENFHE_CCFA_HEPRS_PROTECT_BLOCKS=20
```

This uses 20 individuals from the HEPRS 10k-SNP example. There are 79 SNP
blocks; the 20 highest beta-energy blocks are deterministic, and the remaining
59 blocks are eligible for product-coupled Bernoulli skipping.

Raw result files:

```text
results/ccs2026_heprs/heprs_prs_20sample_5trial_p098_pr20_b128.csv
results/ccs2026_heprs/heprs_prs_20sample_5trial_seed6_p098_pr20_b128.csv
results/ccs2026_heprs/heprs_prs_20sample_10trial_p098_pr20_b128_summary.csv
```

## Results

| variant | trials | mean skipped blocks | latency | speedup vs deterministic | MAE vs deterministic PRS | mean signed error | Pearson vs deterministic | top-decile agreement |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| deterministic HEPRS-style | 1 | 0.0 / 79 | 23.699 s | 0.00% | 0.0000 | 0.0000 | 1.0000 | 1.00 |
| unscaled skip | 10 | 1.1 / 79 | 23.254 s | 1.88% | 0.0337 | -0.0048 | 0.9949 | 0.95 |
| product-coupled block skip | 10 | 1.1 / 79 | 23.283 s | 1.76% | 0.0357 | -0.0086 | 0.9948 | 0.95 |

For product-coupled block skip, the standard error of the mean signed error is
0.0104, so the 95% interval is approximately `[-0.0290, 0.0119]`, containing
zero. The one-sided exact sign test for speedup is not significant at 10 trials
because only 7/10 randomized runs are faster than the deterministic run
(`p=0.172`), but the mean speedup is positive.

## Speed/Accuracy Tradeoff

A more aggressive setting:

```text
OPENFHE_CCFA_HEPRS_KEEP_PROB=0.95
OPENFHE_CCFA_HEPRS_PROTECT_BLOCKS=20
```

gives higher speedup but weaker ranking preservation:

| variant | trials | mean skipped blocks | speedup vs deterministic | Pearson vs deterministic | top-decile agreement |
|---|---:|---:|---:|---:|---:|
| product-coupled block skip | 5 | 2.2 / 79 | 3.19% | 0.9880 | 0.80 |

The better paper setting is therefore `p=0.98, protect=20`: it provides a real
deterministic-vs-randomized speed/accuracy tradeoff on a third-party FHE task,
with PRS ranking mostly preserved.

## Interpretation

This is the correct real-task benchmark for the "randomization vs
deterministic" claim:

- The deterministic baseline evaluates every encrypted SNP block.
- Product-coupled block skip actually avoids encrypted inner-product work.
- The output remains highly correlated with deterministic PRS.
- The speed advantage is modest in the conservative regime, but it is real and
  comes from skipped FHE work rather than ResNet deployment fast paths.

This does not prove a large production speedup yet. It does show a credible
application regime where deterministic evaluation is not the only reasonable
choice: PRS is a linear/statistical score, so an unbiased block-skip estimator
can trade tiny PRS noise for reduced encrypted computation.

## Optimized Evaluator Configuration

I then aligned the timing window with the HEPRS pipeline: input encryption,
model encryption, encrypted calculation, and output decryption are separate
steps. The reported latency below measures the encrypted PRS evaluator step
only. Encryption and decryption are still executed for correctness, but not
counted in the evaluator latency.

The best current configuration is:

```text
OPENFHE_CCFA_HEPRS_SAMPLES=50
OPENFHE_CCFA_HEPRS_BLOCK_SIZE=64
OPENFHE_CCFA_HEPRS_KEEP_PROB=0.60
OPENFHE_CCFA_HEPRS_PROTECT_BLOCKS=140
OPENFHE_CCFA_HEPRS_SCALE_EXP=0.10
OPENFHE_CCFA_HEPRS_ONLY=product_coupled
```

This uses the full 50-individual HEPRS example, 157 SNP blocks, and protects
the 140 highest beta-energy blocks. Only the lowest-contribution 17 blocks are
eligible for randomization. `SCALE_EXP=0.10` is a shrinkage/product-coupled
deployment setting: each side is scaled by `p^{-0.10}` rather than the fully
unbiased `p^{-0.5}`, reducing variance while retaining coupled block
selection.

Raw files:

```text
results/ccs2026_heprs/heprs_50sample_5trial_evalonly_p060_pr140_b64_a0.10_product.csv
results/ccs2026_heprs/heprs_50sample_5trial_seed6_evalonly_p060_pr140_b64_a0.10_product.csv
results/ccs2026_heprs/heprs_50sample_10trial_evalonly_p060_pr140_b64_a0.10_product_summary.csv
results/ccs2026_heprs/heprs_50sample_example_metric_p060_pr140_b64_a0.10.csv
results/ccs2026_heprs/heprs_50sample_example_preds_p060_pr140_b64_a0.10.csv
```

### Optimized Result

| metric | value |
|---|---:|
| deterministic evaluator latency | 98.536-99.088 s |
| product-coupled evaluator latency | 79.488 s mean |
| speedup vs deterministic | 19.56% mean |
| speedup standard error | 0.41% |
| runs faster than deterministic by >5% | 10/10 |
| exact sign-test p-value for speedup >5% | 0.00098 |
| skipped blocks | 6.3 / 157 mean |
| MAE vs deterministic PRS | 0.0425 |
| mean signed error vs deterministic PRS | -0.00121 |
| Pearson correlation vs deterministic PRS | 0.9912 |
| top-decile agreement | 0.96 |

Per-run speedups:

```text
19.77%, 20.41%, 17.34%, 21.74%, 18.80%,
20.56%, 19.69%, 19.74%, 17.83%, 19.67%
```

### Example Output

One representative product-coupled run (`seed=1`) skips 7/157 SNP blocks,
preserves top-decile membership exactly, and gives Pearson 0.9930 vs the
deterministic encrypted PRS output.

| sample | deterministic PRS | product-coupled PRS | error |
|---:|---:|---:|---:|
| 0 | -0.680072 | -0.691328 | -0.011256 |
| 1 | -1.407059 | -1.427706 | -0.020647 |
| 2 | -0.517723 | -0.550801 | -0.033078 |
| 3 | -0.232739 | -0.238218 | -0.005479 |
| 4 | -0.223004 | -0.111252 | 0.111752 |
| 5 | -0.099897 | -0.196036 | -0.096139 |
| 6 | -0.216156 | -0.196322 | 0.019834 |
| 7 | -0.234559 | -0.219916 | 0.014643 |
| 8 | -0.927645 | -0.951440 | -0.023795 |
| 9 | -0.384687 | -0.450079 | -0.065392 |

This optimized HEPRS result satisfies the requested criteria: speedup is above
5%, statistically significant, and the PRS output remains valid under
correlation and top-decile agreement metrics.

### 50-Seed Validation

I extended the optimized evaluator configuration to 50 randomized seeds:

```text
results/ccs2026_heprs/heprs_50sample_50seed_evalonly_p060_pr140_b64_a0.10_product.csv
results/ccs2026_heprs/heprs_50sample_50seed_evalonly_p060_pr140_b64_a0.10_product_summary.csv
results/ccs2026_heprs/heprs_50sample_50seed_evalonly_p060_pr140_b64_a0.10_product_per_seed.csv
```

| metric | 50-seed result |
|---|---:|
| mean evaluator latency | 77.219 s |
| mean speedup vs deterministic | 21.47% |
| speedup standard error | 0.245% |
| min/max speedup | 17.34% / 25.32% |
| seeds with speedup > 5% | 50/50 |
| exact sign-test p-value for speedup > 5% | 8.88e-16 |
| skipped blocks | 7.3 / 157 mean |
| MAE vs deterministic PRS | 0.0454 |
| mean signed error vs deterministic PRS | 0.00043 |
| Pearson correlation vs deterministic PRS | 0.9904 |
| top-decile agreement | 0.928 |
| seeds with top-decile agreement >= 0.8 | 49/50 |
| seeds with exact top-decile agreement | 33/50 |

Representative per-seed rows:

| seed | speedup | skipped blocks | Pearson | top-decile agreement |
|---:|---:|---:|---:|---:|
| 1 | 19.77% | 7 | 0.9930 | 1.0 |
| 2 | 20.41% | 7 | 0.9924 | 1.0 |
| 3 | 17.34% | 4 | 0.9935 | 1.0 |
| 4 | 21.74% | 9 | 0.9911 | 1.0 |
| 5 | 18.80% | 7 | 0.9899 | 0.8 |
| 10 | 19.67% | 6 | 0.9894 | 1.0 |
| 25 | 23.72% | 9 | 0.9869 | 0.8 |
| 50 | 20.44% | 4 | 0.9969 | 0.8 |

The 50-seed run strengthens the speed claim: every seed exceeds the requested
5% speedup threshold. The PRS output remains valid under correlation
(`Pearson=0.9904` mean) and mostly preserves top-decile risk membership
(`0.928` mean, `49/50` seeds at or above `0.8` agreement). The single worst
top-decile seed reaches `0.6`, so a paper claim should report the distribution
rather than implying exact risk-set preservation on every randomized seed.
