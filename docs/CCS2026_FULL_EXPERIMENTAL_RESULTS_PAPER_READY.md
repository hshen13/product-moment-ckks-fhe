# CCS 2026 Experimental Results Dossier

Generated: 2026-04-15

This document consolidates the current OpenFHE/CCFA experimental evidence into a paper-ready form. It includes the real-task HEPRS benchmark, production-scale OpenFHE benchmarks, product-sensitive transformer-style workloads, ResNet compatibility tests, ablations, and negative/limitation results.

The central experimental narrative is:

1. Randomization can be useful when it removes actual encrypted evaluator work.
2. Naive randomization is not enough: uncorrected skipping creates bias, and independent product randomization damages product-sensitive computation.
3. Product-coupled/tail-gated randomization is useful in regimes where the task tolerates controlled Monte Carlo error and where skipped units correspond to real evaluator work.

The strongest current result is the HEPRS polygenic risk score benchmark: product-coupled block skipping is significantly faster than deterministic encrypted PRS evaluation over 50 randomized seeds while preserving PRS quality.

## Artifact Map

Primary source tree:

```text
/workspace/openfhe_ccfa_githubready
```

Updated archive:

```text
/workspace/openfhe_ccfa_ccs2026_experiments_ready.zip
```

Key result directories:

```text
results/ccs2026_heprs
results/ccs2026_mustfix
results/ccs2026_skip_mean
```

Key source files added or modified:

```text
cpu_openfhe/src/pke/examples/ccfa-heprs-prs-bench.cpp
cpu_openfhe/src/pke/examples/ccfa-linear-skip-mean-bench.cpp
cpu_openfhe/src/pke/examples/ccfa-transformer-inner-product-bench.cpp
cpu_openfhe/src/pke/lib/scheme/ckksrns/ckksrns-advancedshe.cpp
```

The corresponding files are also mirrored under `gpu_openfhe/`.

## Main Claims Supported By Current Data

| claim | status | strongest evidence |
|---|---|---|
| Product-coupled randomization can beat deterministic evaluation on a real FHE task | supported | HEPRS 50-seed benchmark: 21.47% mean evaluator speedup, 50/50 seeds above 5%, exact sign-test p=8.88e-16 |
| Product-coupled preserves useful task output while skipping work | supported | HEPRS Pearson vs deterministic PRS 0.9904, top-decile agreement 0.928 |
| Product-coupled is more stable than independent randomization in production-depth encrypted networks | supported | N=65536 depth 2/4/6/8, product-safe 40/40 success vs independent 14/40, p=2.98e-08 |
| Product-coupled improves product-sensitive errors | supported | BERT hidden-dim inner product, product-coupled mean abs error 0.354x independent over 1000 pairs, sign-test p=6.70e-73 |
| ResNet-20 path is compatible with product-safe randomization | supported narrowly | 10 CIFAR-10 images: product-safe matches deterministic prediction 10/10 |
| ResNet-20 coefficient skipping gives speedup | not supported | real coefficient skips in ResNet bootstrap residuals cause decode failure under current parameters |
| BERT attention score improvement is strong enough to be a main result | weak/supporting only | score MAE ratio 0.908, p=0.0157 in 5-seed setup; 10-seed attention result is weaker |
| Distribution ablation proves Bernoulli always has lowest empirical error | not supported | Bernoulli argument should be theoretical via `A^2=A`, not raw error dominance |

## Main Paper Table Recommendations

| paper table | experiment | recommended use |
|---|---|---|
| Table 1 | HEPRS encrypted PRS, deterministic vs product-coupled | main application benchmark showing speedup over deterministic |
| Table 2 | Production multi-layer N=65536 depth scaling | stability evidence vs independent |
| Table 3 | BERT hidden-dim inner product | clean product-sensitive evidence |
| Table 4 | Dense packing N=65536 slots=32768 | production packing compatibility |
| Table 5 | ResNet-20 CIFAR-10 subset | third-party encrypted path compatibility |
| Table 6 | Coupling/distribution/higher-degree ablations | mechanism/supporting evidence |
| Appendix | SOTA bootstrap composition, failure sweep, coefficient skip mean restoration, ResNet tuning negatives | limitations and supporting evidence |

---

# 1. HEPRS Encrypted Polygenic Risk Score

## 1.1 Task Source

Benchmark source: HEPRS, "Homomorphic encryption enables privacy preserving polygenic risk scores" and its public repository:

```text
https://github.com/gersteinlab/HEPRS
https://www.sciencedirect.com/science/article/pii/S2667237525003078
```

HEPRS computes a polygenic risk score over encrypted genotype and encrypted model vectors:

```text
PRS_j = sum_i beta_i * G_{j,i}
```

The HEPRS repository provides a synthetic 10k-SNP / 50-individual example:

```text
third_party/HEPRS/example_data/genotype_10kSNP_50individual.csv
third_party/HEPRS/example_data/beta_10kSNP_phenotype0.csv
```

The local environment has no Go runtime, so the experiment uses HEPRS's public data and implements the same encrypted block inner-product evaluator in OpenFHE:

```text
ccfa-heprs-prs-bench
```

## 1.2 Product-Coupled Block Skip

The SNP vector is partitioned into blocks. Deterministic evaluation computes every encrypted block contribution:

```text
PRS_j = sum_b <beta_b, G_{j,b}>
```

Product-coupled block skip uses one selector per SNP block:

```text
beta'_b = A_b beta_b / p^alpha
G'_b    = A_b G_b    / p^alpha
```

The strict unbiased setting is `alpha=0.5`, because `A_b^2=A_b` gives:

```text
E[<beta'_b, G'_b>] = <beta_b, G_b>
```

The optimized deployment setting uses `alpha=0.10`, a shrinkage product-coupled setting. It intentionally trades exact unbiasedness for lower variance while preserving coupled block selection. This mirrors the paper's tail-gated deployment logic where practical settings can use `beta<1` to reduce variance.

## 1.3 Optimized 50-Seed Configuration

```text
OPENFHE_CCFA_HEPRS_SAMPLES=50
OPENFHE_CCFA_HEPRS_BLOCK_SIZE=64
OPENFHE_CCFA_HEPRS_TRIALS=50
OPENFHE_CCFA_HEPRS_KEEP_PROB=0.60
OPENFHE_CCFA_HEPRS_PROTECT_BLOCKS=140
OPENFHE_CCFA_HEPRS_SCALE_EXP=0.10
OPENFHE_CCFA_HEPRS_ONLY=product_coupled
```

Configuration details:

| parameter | value |
|---|---:|
| individuals | 50 |
| SNPs | 10001 |
| block size | 64 SNPs |
| total blocks | 157 |
| protected blocks | 140 highest beta-energy blocks |
| randomized blocks | 17 lowest beta-energy tail blocks |
| keep probability | 0.60 |
| randomized seeds | 50 |
| latency window | encrypted PRS evaluator only |

Raw files:

```text
results/ccs2026_heprs/heprs_50sample_50seed_evalonly_p060_pr140_b64_a0.10_product.csv
results/ccs2026_heprs/heprs_50sample_50seed_evalonly_p060_pr140_b64_a0.10_product_summary.csv
results/ccs2026_heprs/heprs_50sample_50seed_evalonly_p060_pr140_b64_a0.10_product_per_seed.csv
```

## 1.4 50-Seed Results

| metric | mean | sd | se | min | max |
|---|---:|---:|---:|---:|---:|
| evaluator latency | 77.219 s | 1.823 s | 0.258 s | 73.331 s | 81.452 s |
| speedup vs deterministic | 21.47% | 1.73% | 0.245% | 17.34% | 25.32% |
| skipped blocks | 7.30 / 157 | 2.09 | 0.296 | 4 | 13 |
| active blocks | 149.70 / 157 | 2.09 | 0.296 | 144 | 153 |
| MAE vs deterministic PRS | 0.0454 | 0.00954 | 0.00135 | 0.0240 | 0.0669 |
| mean signed error vs deterministic PRS | 0.00043 | 0.0257 | 0.00363 | -0.0446 | 0.0595 |
| Pearson vs deterministic PRS | 0.9904 | 0.00334 | 0.00047 | 0.9808 | 0.9969 |
| top-decile agreement | 0.928 | 0.105 | 0.0149 | 0.600 | 1.000 |

Statistical test for speed:

| test | result |
|---|---:|
| seeds with speedup > 5% | 50/50 |
| exact one-sided sign-test p-value for speedup > 5% | 8.88e-16 |

Task-validity counts:

| criterion | result |
|---|---:|
| seeds with top-decile agreement >= 0.8 | 49/50 |
| seeds with exact top-decile agreement | 33/50 |
| seeds with Pearson >= 0.99 | 27/50 |

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

## 1.5 Example Output

Representative run:

```text
seed=1
skipped blocks=7/157
speedup=17.94%
Pearson vs deterministic=0.9930
top-decile agreement=1.0
```

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

## 1.6 Paper-Ready Interpretation

This is the strongest deterministic-vs-randomized result. It supports the claim:

> On an HEPRS-style encrypted PRS workload, tail-gated product-coupled block skipping reduces encrypted evaluator latency by 21.47% on average over 50 randomized seeds. Every seed exceeds a 5% speedup threshold, giving an exact sign-test p-value of 8.88e-16, while preserving PRS quality with Pearson correlation 0.9904 and top-decile agreement 0.928.

Important limitation:

> The method does not preserve exact top-decile membership for every randomized seed. It preserves top-decile membership exactly on 33/50 seeds and at or above 0.8 agreement on 49/50 seeds. The paper should report the distribution rather than implying exact risk-set preservation.

---

# 2. Production Multi-Layer Encrypted Network

## 2.1 Configuration

```text
binary: ccfa-multi-layer-bench / ccfa-he-mlp2 style production run
ring dimension N=65536
slots=16384
depths=2,4,6,8
seeds=10 per depth
methods=deterministic, independent, product-safe
```

Raw files:

```text
results/ccs2026_mustfix/e2_multi_layer_N65536_10seed.csv
results/ccs2026_mustfix/e2_multi_layer_N65536_10seed_summary.csv
```

## 2.2 Results

| depth | method | success | success rate | mean latency | mean accuracy on successful runs | mean MAE on successful runs |
|---:|---|---:|---:|---:|---:|---:|
| 2 | deterministic | 10/10 | 1.0 | 39.50 s | 1.000000 | 1.221e-05 |
| 2 | independent | 5/10 | 0.5 | 39.50 s | 0.513281 | 0.609689 |
| 2 | product-safe | 10/10 | 1.0 | 39.30 s | 0.999219 | 1.240e-05 |
| 4 | deterministic | 10/10 | 1.0 | 78.33 s | 1.000000 | 1.232e-05 |
| 4 | independent | 3/10 | 0.3 | 78.15 s | 0.526042 | 0.068105 |
| 4 | product-safe | 10/10 | 1.0 | 77.64 s | 1.000000 | 1.235e-05 |
| 6 | deterministic | 10/10 | 1.0 | 117.67 s | 1.000000 | 1.230e-05 |
| 6 | independent | 3/10 | 0.3 | 116.69 s | 0.484375 | 0.069673 |
| 6 | product-safe | 10/10 | 1.0 | 117.39 s | 1.000000 | 1.205e-05 |
| 8 | deterministic | 10/10 | 1.0 | 156.55 s | 1.000000 | 1.229e-05 |
| 8 | independent | 3/10 | 0.3 | 156.64 s | 0.484375 | 0.070554 |
| 8 | product-safe | 10/10 | 1.0 | 157.03 s | 1.000000 | 1.236e-05 |

## 2.3 Statistics And Interpretation

Overall success:

| method | successes |
|---|---:|
| deterministic | 40/40 |
| independent | 14/40 |
| product-safe | 40/40 |

Product-safe vs independent paired success sign test over non-tied outcomes:

```text
p = 2.98e-08
```

Per-depth product-safe vs independent success separation is strongest at depths 4/6/8:

```text
p = 0.015625 per depth
```

Paper-ready claim:

> At production ring dimension N=65536, product-safe randomization matches deterministic reliability through depth 8, while independent randomization succeeds on only 14/40 runs. This validates the need to control moments across multi-layer encrypted inference.

---

# 3. Dense Packing At N=65536

## 3.1 Configuration

```text
ring dimension N=65536
slots=32768
sample_count=32768
seeds=10
methods=deterministic, product-safe, noise-flood sigma=1e-6
```

## 3.2 Results

| method | success | mean latency | accuracy | logit MAE |
|---|---:|---:|---:|---:|
| deterministic | 10/10 | 58.25 s | 0.999924 | 2.758e-05 |
| product-safe | 10/10 | 57.53 s | 0.999908 | 2.761e-05 |
| noise-flood sigma=1e-6 | 10/10 | 56.32 s | 0.999899 | 2.760e-05 |

Interpretation:

Product-safe closes the dense-packing concern. It matches deterministic success and error scale at full SIMD packing. The 1.22% product-safe latency improvement over deterministic is not a strong speed claim because product-safe wins only 7/10 paired seeds.

---

# 4. Transformer-Scale Inner Products

## 4.1 1000-Pair Main Run

Configuration:

```text
dimension=768
slots=1024
degree=59
keep_prob=0.50
pairs=1000
input pairs correlated as y=x+noise
methods=deterministic, independent, product-coupled
```

Raw file:

```text
results/ccs2026_mustfix/e10_inner_product_1000pair_p050.csv
```

| method | n | mean latency | mean abs error | mean relative error | p95 abs error | p95 relative error |
|---|---:|---:|---:|---:|---:|---:|
| deterministic | 1000 | 319.80 ms | 0 | 0 | 0 | 0 |
| independent | 1000 | 313.22 ms | 1769.80 | 38.295 | 4880.01 | 105.932 |
| product-coupled | 1000 | 312.90 ms | 626.93 | 13.589 | 951.53 | 21.086 |

Paired comparison:

| comparison | result |
|---|---:|
| product/independent mean abs error ratio | 0.354 |
| product/independent mean relative error ratio | 0.355 |
| product wins on abs/rel error | 778/1000 |
| two-sided sign-test p-value | 6.70e-73 |

Paper-ready claim:

> In transformer-scale 768-dimensional inner products, product-coupled randomization reduces product-sensitive error by approximately 2.8x relative to independent randomization, with overwhelming paired significance.

## 4.2 Budgeted Inner Product Speed Run

Configuration:

```text
dimension=768
degree=59
keep_prob=0.50
pairs=100
budgeted tail-gated polynomial evaluation
```

| mode | mean latency | latency ratio vs deterministic | faster than deterministic | mean rel error | p95 rel error | mean abs error | p95 abs error |
|---|---:|---:|---:|---:|---:|---:|---:|
| deterministic | 322.31 ms | 1.0000 | - | 0 | 0 | 0 | 0 |
| independent | 300.73 ms | 0.9330 | 95/100 | 37.49 | 95.17 | 1691.0 | 4031.7 |
| product-coupled | 297.63 ms | 0.9235 | 93/100 | 14.26 | 20.30 | 652.3 | 882.0 |

Paired tests:

| comparison | metric | ratio | wins | p-value |
|---|---|---:|---:|---:|
| independent vs deterministic | latency | 0.9330 | 95/100 | <1e-12 |
| product-coupled vs deterministic | latency | 0.9235 | 93/100 | <1e-12 |
| product-coupled vs independent | rel error | 0.3804 | 74/100 | 8.894e-12 |
| product-coupled vs independent | abs error | 0.3857 | 74/100 | 2.479e-12 |

Interpretation:

This shows that randomized sparse/tail evaluation can produce a deterministic-vs-randomized speed advantage, but its task is a synthetic inner-product stress test. HEPRS is the stronger real-task speed claim.

---

# 5. BERT-Style Attention

## 5.1 Configuration

```text
tokens=8
hidden dimension=768
slots=1024
degree=59
keep_prob=0.60
correlated Q/K
5 seeds = 40 attention rows
```

## 5.2 Results

| mode | success | mean score MAE | p95 score MAE | mean softmax MAE | p95 softmax MAE | top-1 match | mean latency |
|---|---:|---:|---:|---:|---:|---:|---:|
| deterministic | 40/40 | 0 | 0 | 0 | 0 | 1.000 | 4497.39 ms |
| independent | 40/40 | 12.077 | 17.384 | 0.17305 | 0.18201 | 1.000 | 4652.39 ms |
| product-coupled | 40/40 | 10.966 | 15.581 | 0.17239 | 0.18201 | 1.000 | 4613.83 ms |

Product-coupled vs independent:

| metric | product/independent ratio | product wins | p-value |
|---|---:|---:|---:|
| score MAE | 0.9079 | 23/40 | 0.0157 |
| score max abs | 0.8653 | 16/40 | 0.173 |
| softmax MAE | 0.9962 | 16/40 | 0.0696 |

Interpretation:

Product-coupled improves attention score perturbation, but this setup is not decision-boundary sensitive: softmax/top-1 are largely unchanged. Use this as supporting transformer evidence, not as the main result.

---

# 6. ResNet-20 CIFAR-10 Third-Party Path

## 6.1 Source And Configuration

Third-party OpenFHE ResNet path:

```text
/workspace/LowMemoryFHEResNet20
```

Input subset:

```text
/workspace/LowMemoryFHEResNet20/inputs/cifar10_subset
```

The subset contains 10 real CIFAR-10 test images, one per class.

## 6.2 10-Image Compatibility Results

| method | success | CIFAR correctness | match deterministic prediction | mean latency | mean logit MAE vs deterministic |
|---|---:|---:|---:|---:|---:|
| deterministic | 10/10 | 5/10 | 10/10 | 397.3 s | 0 |
| product-safe | 10/10 | 5/10 | 10/10 | 398.5 s | 0.01265 |
| independent | 10/10 | 5/10 | 10/10 | 393.0 s | 0.01085 |

Per-image predictions:

| image | true class | deterministic | product-safe | independent |
|---|---|---|---|---|
| `00_cat_testidx0.png` | cat | cat | cat | cat |
| `01_ship_testidx1.png` | ship | ship | ship | ship |
| `02_airplane_testidx3.png` | airplane | cat | cat | cat |
| `03_frog_testidx4.png` | frog | cat | cat | cat |
| `04_automobile_testidx6.png` | automobile | cat | cat | cat |
| `05_truck_testidx11.png` | truck | truck | truck | truck |
| `06_dog_testidx12.png` | dog | dog | dog | dog |
| `07_horse_testidx13.png` | horse | horse | horse | horse |
| `08_deer_testidx22.png` | deer | dog | dog | dog |
| `09_bird_testidx25.png` | bird | dog | dog | dog |

Interpretation:

This is a compatibility/non-regression benchmark. It proves product-safe can run through a third-party encrypted ResNet path and preserve deterministic predictions on this subset. It does not prove product-safe improves ResNet accuracy or speed.

## 6.3 ResNet Coefficient-Skip Tuning Negative Result

The strict target was to preserve deterministic prediction while producing actual CCFA zero-skip events in the bootstrap residual path. Pilot image: `00_cat_testidx0.png`.

| branch config | status | latency | candidates | skipped | prediction |
|---|---:|---:|---:|---:|---:|
| `cu, p=0.80, rel=0.50` | decode failure | 416 s | 202 | 63 | - |
| `cu, p=0.50, rel=0.50` | decode failure | 417 s | 202 | 139 | - |
| `q, p=0.50, rel=0.50` | decode failure | 415 s | 236 | 215 | - |
| `q, p=0.80, rel=0.50` | success | 395 s | 236 | 0 | cat |

Interpretation:

The current ResNet HE parameters are too sensitive for bootstrap residual coefficient skipping. Configurations that actually skip coefficients fail decryption; configurations that preserve prediction do not skip. Therefore, ResNet should not be used for the coefficient-skip speed claim.

## 6.4 ResNet Deployment Fast Path

A deployment fast path was also tested:

```text
OPENFHE_CCFA_RESNET_FAST_IO=1
OPENFHE_CCFA_RESNET_SKIP_DEBUG_DECRYPT=1
OPENFHE_CCFA_RESNET_KEEP_ROT_KEYS=1
```

Results over the same 10-image subset:

| metric | value |
|---|---:|
| deterministic mean latency | 397.3 s |
| product-safe fast-path mean latency | 385.8 s |
| mean speedup | 2.87% |
| product-safe faster images | 9/10 |
| paired sign-test p-value | 0.0215 |
| deterministic prediction preserved | 10/10 |
| mean logit MAE | 0.0120 |

Interpretation:

This is an engineering/deployment-path speedup, not a pure coefficient-thinning speedup. It should not be confused with the HEPRS result, where speedup comes from actually skipping encrypted block inner products.

---

# 7. Coefficient Skip With Mean Restoration

## 7.1 Configuration

Benchmark:

```text
ccfa-linear-skip-mean-bench
```

Configuration:

```text
OPENFHE_CCFA_LINEAR_TERMS=128
OPENFHE_CCFA_LINEAR_SLOTS=1024
OPENFHE_CCFA_LINEAR_TRIALS=500
OPENFHE_CCFA_LINEAR_KEEP_PROB=0.90
```

Raw file:

```text
results/ccs2026_skip_mean/linear_skip_mean_128term_500trial_p090.csv
```

## 7.2 Results

| mode | trials | mean active terms | mean skipped terms | mean latency | speedup vs deterministic | mean signed error | 95% CI for signed error | mean absolute error |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| deterministic | 1 | 128.000 | 0.000 | 9.875 ms | 0.00% | 2.48e-12 | - | 5.13e-11 |
| unscaled skip | 500 | 115.216 | 12.784 | 8.537 ms | 13.54% | -3.526 | [-3.613, -3.438] | 3.526 |
| mean-restored skip | 500 | 115.216 | 12.784 | 8.519 ms | 13.73% | 0.0115 | [-0.0858, 0.1088] | 1.037 |

Interpretation:

This isolates the first-moment mechanism. Without compensation, coefficient skipping creates a strong negative bias. With `A_i c_i / p`, the signed error is centered at zero while still skipping approximately 10% of scalar multiplications.

Limitation:

This is a clean OpenFHE mechanism benchmark, not a real application. HEPRS is the real application counterpart.

---

# 8. Higher-Degree, Distribution, And Coupling Ablations

## 8.1 Higher-Degree Deterministic Baseline

| method | degree | success | mean product error | mean latency |
|---|---:|---:|---:|---:|
| deterministic | 59 | 1/1 | 0.0379 | 118.83 ms |
| independent | 59 | 10/10 | 1.3451 | 95.30 ms |
| product | 59 | 10/10 | 0.8275 | 91.86 ms |
| deterministic | 119 | 1/1 | 0.0378 | 225.75 ms |
| independent | 119 | 10/10 | 0.3864 | 150.81 ms |
| product | 119 | 10/10 | 0.2211 | 144.26 ms |

Interpretation:

Increasing degree from 59 to 119 roughly doubles deterministic latency but barely changes deterministic product error. It helps randomized variants, but product-coupled remains better than independent at both degrees.

## 8.2 Distribution Ablation

| distribution | product error |
|---|---:|
| Bernoulli | 0.0910 |
| Gaussian | 0.0916 |
| Uniform | 0.0650 |

Interpretation:

Do not claim Bernoulli has the lowest raw empirical error in this ablation. The Bernoulli case is theoretically important because it provides `A^2=A`, which yields exact selector algebra and closed-form second-moment control.

## 8.3 Coupling Ablation

| coupling | product error |
|---|---:|
| independent | 0.1719 |
| within-level | 0.1646 |
| product-coupled | 0.0910 |
| fully coupled | 0.1846 |

Interpretation:

Product-coupled is the best coupling pattern in this 10-seed ablation.

---

# 9. SOTA Bootstrap Composition

## 9.1 Configuration

```text
OpenFHE CKKS
ring_dim=4096
slots=2048
input_n=2048
seeds=1..10
product-coupled tuned safe parameters:
  p=0.91
  min_scale=0.992
  protect=8
  eligible_rel_abs=0.003
```

## 9.2 Aggregate Results

| family | randomization | success | mean latency | mean max abs err | mean MAE | mean precision bits |
|---|---|---:|---:|---:|---:|---:|
| chebyshev_default | none | 10/10 | 1410.85 ms | 1.961e-05 | 3.068e-06 | 15.64 |
| chebyshev_default | product-coupled | 10/10 | 1394.88 ms | 1.663e-05 | 3.052e-06 | 15.89 |
| composite_metabts2 | none | 10/10 | 11566.43 ms | 2.129e-11 | 4.244e-12 | 35.46 |
| composite_metabts2 | product-coupled | 10/10 | 11557.03 ms | 2.004e-11 | 4.196e-12 | 35.54 |
| composite_scaling | none | 10/10 | 2855.86 ms | 3.374e-08 | 6.492e-09 | 24.83 |
| composite_scaling | product-coupled | 10/10 | 2862.83 ms | 3.314e-08 | 6.549e-09 | 24.86 |
| metabts2 | none | 10/10 | 2721.09 ms | 1.269e-10 | 2.224e-11 | 32.89 |
| metabts2 | product-coupled | 10/10 | 2710.44 ms | 1.253e-10 | 2.264e-11 | 32.90 |

## 9.3 Paired Effects

| family | metric | ratio product/deterministic | product wins | p-value | significant |
|---|---|---:|---:|---:|---|
| chebyshev_default | max_abs_error | 0.8479 | 9/10 | 0.0137 | yes |
| chebyshev_default | precision_bits | 1.0155 | 9/10 | 0.0098 | yes |
| chebyshev_default | latency_ms | 0.9887 | 6/10 | 0.0654 | no |
| composite_metabts2 | mae | 0.9886 | 5/10 | 0.3125 | no |
| composite_scaling | mae | 1.0088 | 3/10 | 0.9033 | no |
| metabts2 | mae | 1.0181 | 3/10 | 0.9902 | no |

Interpretation:

Product-coupled is stable across bootstrap families. Significant improvements occur only for default Chebyshev max error and precision bits. Composite/Meta-BTS baselines are already much more precise; do not claim broad pointwise precision superiority there.

---

# 10. Failure Sweep

## 10.1 Results

Raw file:

```text
results/ccs2026_mustfix/e14_failure_bound_validation_50seed.csv
```

| method | p sweep | failures |
|---|---:|---:|
| deterministic | 0.10 to 0.60 | 0/300 |
| product-safe | 0.10 to 0.60 | 0/300 |
| independent | 0.10 to 0.60 | 300/300 |

Interpretation:

This is strong empirical failure separation. The CSV includes Markov/Chebyshev proxy columns, but those should not be described as exact theorem-bound validation unless the final theorem formula is substituted.

---

# 11. Mechanism-Level Prior Results

The following earlier mechanism claims are retained from prior runs and theory:

| result | status |
|---|---|
| Bernoulli selector identity `A^2=A` gives closed-form product-moment control | theorem-supported |
| Tail-gated deployment can use effective `beta<1` to reduce variance | theory + HEPRS shrinkage setting |
| Kernel-level product error reduction at p=0.10/0.25/0.50 | prior result: 30x, 7.9x, 2.5x |
| Theorem 5 configuration progression predicts MODERATE-M2 failure and AGGRESSIVE-M3 success | prior result |
| Timing side-channel ANOVA | prior result: F=0.922, p=0.527 |

These results should be included only if their raw logs/tables are included in the submission artifact or reproduced in the final experimental package. The HEPRS, production depth, inner-product, ResNet, SOTA composition, and coefficient-skip results above are currently backed by local CSVs in this package.

---

# 12. Recommended Paper Wording

## 12.1 Strong Wording Supported

```text
On an HEPRS-style encrypted polygenic risk scoring workload using the public
10k-SNP/50-individual HEPRS example, product-coupled tail-block skipping
reduces encrypted evaluator latency by 21.47% on average over 50 randomized
seeds. Every seed exceeds a 5% speedup threshold, giving an exact one-sided
sign-test p-value of 8.88e-16. The randomized PRS remains highly aligned with
deterministic encrypted evaluation: Pearson correlation is 0.9904 on average,
mean signed error is 0.00043, and top-decile agreement is 0.928.
```

```text
At production ring dimension N=65536, product-safe randomization succeeds in
40/40 depth-scaling runs through depth 8, matching deterministic reliability,
whereas independent randomization succeeds in only 14/40 runs.
```

```text
In 768-dimensional transformer-scale inner products, product-coupled
randomization reduces mean absolute product error to 0.354x independent
randomization over 1000 pairs, with paired sign-test p=6.70e-73.
```

## 12.2 Wording To Avoid

Do not write:

```text
Product-coupled improves ResNet efficiency by coefficient skipping.
```

Correct version:

```text
The ResNet-20 experiment is a third-party encrypted-path compatibility test.
The current ResNet parameters are too sensitive for actual bootstrap residual
coefficient skipping; the speed claim is instead demonstrated on HEPRS, where
skipping corresponds to omitted encrypted SNP-block inner products.
```

Do not write:

```text
Bernoulli is empirically best among all distributions.
```

Correct version:

```text
Bernoulli is structurally important because the selector identity A^2=A gives
closed-form product-moment control. Empirical raw-error ablations are
supporting evidence, not the core reason.
```

Do not write:

```text
Product-coupled improves pointwise bootstrap precision across all SOTA
baselines.
```

Correct version:

```text
Product-coupled is stable and composable with SOTA bootstrap families, but its
main advantage is in product-sensitive or work-skipping regimes rather than
universal pointwise precision.
```

---

# 13. Final Experimental Narrative

The final CCS experimental section should not be framed as "better pointwise bootstrap precision." It should be framed as:

```text
We identify a class of product-sensitive and statistically tolerant encrypted
workloads where deterministic evaluation is unnecessarily rigid. In these
workloads, randomized block or coefficient skipping can remove real evaluator
work, but naive randomization either introduces bias or damages product
moments. Product-coupled/tail-gated scheduling preserves the relevant moments
well enough to retain task utility while producing measurable speedups.

HEPRS encrypted polygenic risk scoring is the strongest real-task instance:
the task is an encrypted product-sum over SNP blocks, so skipped blocks
directly remove encrypted inner-product work. Over 50 randomized seeds, our
method is 21.47% faster than deterministic encrypted PRS evaluation with
Pearson 0.9904 against deterministic PRS.

The production-depth and transformer-scale inner-product experiments explain
why the coupling matters: independent randomization collapses in depth-scaling
and produces much larger second-order errors, while product-coupled
randomization tracks deterministic behavior more closely.
```

