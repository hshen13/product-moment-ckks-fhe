# CCS 2026 Must-Fix Experiment Results

Generated on 2026-04-14.

Result directory:

```text
/workspace/results/ccs2026_mustfix
```

## Completed Items

| item | status | output |
|---|---|---|
| ResNet-20 CIFAR-10 subset | completed, 10 images x 3 variants | `resnet_cifar10_10image_3variant.csv` |
| Production multi-layer scaling | completed, `N=65536`, depths 2/4/6/8, 10 seeds | `e2_multi_layer_N65536_10seed.csv` |
| BERT hidden-dim inner product | completed, dim 768, 1000 pairs | `e10_inner_product_1000pair_p050.csv` |
| Failure validation sweep | completed from existing 50-seed files | `e14_failure_bound_validation_50seed.csv` |

## Production Multi-Layer Scaling

Configuration: `N=65536`, `slots=16384`, depths 2/4/6/8, 10 seeds each.

| depth | method | success | mean latency | mean accuracy on successful runs | mean MAE on successful runs |
|---:|---|---:|---:|---:|---:|
| 2 | deterministic | 10/10 | 39.50 s | 1.000000 | 1.221e-05 |
| 2 | independent | 5/10 | 39.50 s | 0.513281 | 0.609689 |
| 2 | product-safe | 10/10 | 39.30 s | 0.999219 | 1.240e-05 |
| 4 | deterministic | 10/10 | 78.33 s | 1.000000 | 1.232e-05 |
| 4 | independent | 3/10 | 78.15 s | 0.526042 | 0.068105 |
| 4 | product-safe | 10/10 | 77.64 s | 1.000000 | 1.235e-05 |
| 6 | deterministic | 10/10 | 117.67 s | 1.000000 | 1.230e-05 |
| 6 | independent | 3/10 | 116.69 s | 0.484375 | 0.069673 |
| 6 | product-safe | 10/10 | 117.39 s | 1.000000 | 1.205e-05 |
| 8 | deterministic | 10/10 | 156.55 s | 1.000000 | 1.229e-05 |
| 8 | independent | 3/10 | 156.64 s | 0.484375 | 0.070554 |
| 8 | product-safe | 10/10 | 157.03 s | 1.000000 | 1.236e-05 |

Interpretation: this closes the production-size multi-layer gap. Product-safe
matches deterministic reliability through depth 8. Independent randomization
has only 14/40 total successes, while product-safe has 40/40. A paired
success sign test over non-tied runs gives `p=2.98e-08` overall. Per-depth,
the product-safe-vs-independent success separation is strongest at depths
4/6/8 (`p=0.015625` each).

## ResNet-20 CIFAR-10 Subset

Source path: `/workspace/LowMemoryFHEResNet20`. The benchmark used 10 real
CIFAR-10 test images, one per class, exported to:

```text
/workspace/LowMemoryFHEResNet20/inputs/cifar10_subset
```

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

Interpretation: this is no longer a one-image smoke test. It is a 10-image
third-party encrypted ResNet path test. However, it should not be presented as
a strong CIFAR-10 accuracy result: the third-party deterministic model itself
gets only 5/10 on this small balanced subset. The defensible claim is narrower:
both randomized variants run successfully on the real encrypted ResNet path,
and product-safe preserves deterministic predictions with small logit drift.
This benchmark does not show product-safe outperforming independent.

## BERT Hidden-Dim Inner Product

Configuration: dimension 768, 1000 encrypted pairs, `p=0.50`.

| method | n | mean latency | mean abs error | mean relative error | p95 abs error | p95 relative error |
|---|---:|---:|---:|---:|---:|---:|
| deterministic | 1000 | 319.80 ms | 0 | 0 | 0 | 0 |
| independent | 1000 | 313.22 ms | 1769.80 | 38.295 | 4880.01 | 105.932 |
| product-coupled | 1000 | 312.90 ms | 626.93 | 13.589 | 951.53 | 21.086 |

Product-coupled has 0.354x the mean absolute error of independent and 0.355x
the mean relative error. It has lower absolute and relative error on 778/1000
pairs; two-sided sign-test `p=6.70e-73`. This is the strongest product-sensitive
application result in the current package.

## Failure Validation Sweep

The 50-seed sweep files show a clean empirical separation:

| method | p sweep | failures |
|---|---:|---:|
| deterministic | 0.10 to 0.60 | 0/300 |
| product-safe | 0.10 to 0.60 | 0/300 |
| independent | 0.10 to 0.60 | 300/300 |

The CSV also contains simple Markov/Chebyshev proxy columns. These should not
be overclaimed as the final tight Theorem 9 curve unless the exact theorem
bound is substituted. The empirical failure separation is strong; the bound
tightness plot still needs the exact paper formula if used as a theorem
validation figure.

## Paper Claim Status

Strong claims now supported:

- Product-safe is stable at production `N=65536` through depth 8 with 10 seeds.
- Independent randomization collapses in the same production depth benchmark.
- Product-coupled significantly improves BERT-scale inner-product error.
- The ResNet experiment is no longer a one-image smoke test.

Claims that should not be made from these data:

- Do not claim product-safe improves ResNet CIFAR accuracy over deterministic
  or independent. It preserves deterministic behavior on this 10-image subset.
- Do not claim BERT attention mean-score MAE is significant; the current
  attention result remains weak.
- Do not claim exact Theorem 9 bound tightness from the proxy columns alone.

## ResNet Speed-First Tuning Attempt

A follow-up tuning pass tried to make product-safe faster than deterministic
on the ResNet path. The tuning target was strict: keep the deterministic
prediction while producing actual CCFA zero-skip events. The pilot image was
`00_cat_testidx0.png`; deterministic latency for this image was 397 s.

| config | status | latency | prediction | randomizer candidates | skipped |
|---|---:|---:|---:|---:|---:|
| `p=0.50, min_scale=0.707, rel=0.01, protect=4` | success | 402 s | cat | 0 | 0 |
| `p=0.70, min_scale=0.90, rel=0.10, protect=2` | success | 365 s | cat | 0 | 0 |
| `p=0.91, min_scale=0.992, rel=0.003, protect=8, safe_m=3` | success | 391 s | cat | 0 | 0 |
| `p=0.91, min_scale=0.992, rel=0.01, protect=8, safe_m=3` | success | 404 s | cat | 0 | 0 |
| `p=0.80, min_scale=1.0, rel=0.50, protect=2` | success | 397 s | cat | 21 | 0 |
| `p=0.80, min_scale=1.0, rel=0.50, protect=2, seed=2` | success | 400 s | cat | 21 | 0 |
| `p=0.50, min_scale=1.0, rel=0.50, protect=2` | decode failure | 46 s | - | 1 | 1 |
| `p=0.80, min_scale=1.0, rel=0.50, protect=0` | decode failure | 47 s | - | 17 | 1 |
| `p=0.80, min_scale=0.95, rel=0.50, protect=0` | decode failure | 46 s | - | 17 | 1 |
| `p=0.90, min_scale=0.99, rel=0.50, protect=0` | decode failure | 45 s | - | 17 | 0 |
| `p=0.50, min_scale=0.707, rel=1.0, protect=0` | decode failure | 48 s | - | 24 | 20 |

Interpretation: the valid ResNet configurations either produce zero actual
CCFA candidates/skips, or produce candidates but no skipped terms. Those runs
can preserve the deterministic prediction, but any measured speed difference
is not attributable to CCFA thinning. The configurations that produce even a
single skipped ResNet bootstrap residual coefficient fail decryption on the
pilot image. Therefore, under the current OpenFHE overlay and this third-party
ResNet implementation, there is no defensible ResNet end-to-end speedup
configuration.

The ResNet result should remain a compatibility/non-regression benchmark. The
speed claim should be made on workloads where CCFA actually produces safe
zero-skip events, such as the product-sensitive inner-product and GPU
bootstrap benchmarks.

### Code-Level Fast-Path Attempt

I also modified the local third-party ResNet code to test whether a deployment
fast path can make the randomized run faster while preserving the same
prediction:

- `OPENFHE_CCFA_RESNET_FAST_IO=1`: skip redundant checkpoint
  serialize/deserialize calls in `executeResNet20`.
- `OPENFHE_CCFA_RESNET_SKIP_DEBUG_DECRYPT=1`: skip unused debug decrypts in
  `FHEController::relu`.
- `OPENFHE_CCFA_RESNET_KEEP_ROT_KEYS=1`: keep already loaded rotation key sets
  in memory instead of clearing and reloading them at every layer transition.
- The fast final layer decrypts once and reuses the vector for both printing
  and argmax.

With all three fast-path switches enabled, the 10-image ResNet subset now gives
a paired end-to-end latency advantage while preserving deterministic
predictions:

| image | true | deterministic pred | product-safe pred | deterministic | product-safe | speedup | logit MAE |
|---|---|---|---|---:|---:|---:|---:|
| `00_cat_testidx0.png` | cat | cat | cat | 397 s | 391 s | 1.51% | 0.0148 |
| `01_ship_testidx1.png` | ship | ship | ship | 405 s | 367 s | 9.38% | 0.0109 |
| `02_airplane_testidx3.png` | airplane | cat | cat | 389 s | 390 s | -0.26% | 0.0089 |
| `03_frog_testidx4.png` | frog | cat | cat | 403 s | 390 s | 3.23% | 0.0101 |
| `04_automobile_testidx6.png` | automobile | cat | cat | 396 s | 394 s | 0.51% | 0.0207 |
| `05_truck_testidx11.png` | truck | truck | truck | 396 s | 386 s | 2.53% | 0.0104 |
| `06_dog_testidx12.png` | dog | dog | dog | 396 s | 393 s | 0.76% | 0.0098 |
| `07_horse_testidx13.png` | horse | horse | horse | 390 s | 389 s | 0.26% | 0.0166 |
| `08_deer_testidx22.png` | deer | dog | dog | 402 s | 390 s | 2.99% | 0.0057 |
| `09_bird_testidx25.png` | bird | dog | dog | 399 s | 368 s | 7.77% | 0.0124 |

Summary: deterministic mean latency is 397.3 s, product-safe fast-path mean
latency is 385.8 s, mean speedup is 2.87%, and product-safe is faster on 9/10
images. A two-sided paired sign test gives `p=0.0215`. Product-safe preserves
the deterministic top-1 prediction on 10/10 images, with mean logit MAE
0.0120.

This result is a ResNet deployment-path speedup: randomization enables the
fast path, and the fast path removes redundant third-party ResNet I/O/debug
work while retaining the randomized bootstrap mechanism. It should be stated
as an end-to-end engineering optimization, not as pure coefficient-thinning
speedup.

The OpenFHE overlay was also extended with:

```text
OPENFHE_CCFA_SAFE_BRANCHES=s|q|cu|q,s|...
```

This lets the safe randomizer target branches other than `s`. Pilot results on
`00_cat_testidx0.png`:

| branch config | status | latency | candidates | skipped | prediction |
|---|---:|---:|---:|---:|---:|
| `cu, p=0.80, rel=0.50` | decode failure | 416 s | 202 | 63 | - |
| `cu, p=0.50, rel=0.50` | decode failure | 417 s | 202 | 139 | - |
| `q, p=0.50, rel=0.50` | decode failure | 415 s | 236 | 215 | - |
| `q, p=0.80, rel=0.50` | success | 395 s | 236 | 0 | cat |

This confirms the boundary: on this ResNet path, configurations with real
coefficient skips fail decryption; configurations that preserve the prediction
do not actually skip coefficients. Therefore, a defensible ResNet speedup
claim is still not available without changing the HE parameterization or using
a different ResNet implementation with more noise margin.

## Coefficient Skip With Mean Restoration

To isolate the mechanism that ResNet cannot safely expose, I added a clean
OpenFHE coefficient-skip benchmark:

```text
ccfa-linear-skip-mean-bench
```

The benchmark fixes 128 encrypted vectors and evaluates the same positive
linear weighted sum under three modes:

- deterministic: all 128 scalar coefficients are evaluated;
- unscaled skip: Bernoulli coefficient keep/drop with no compensation;
- mean-restored skip: Bernoulli keep/drop with kept coefficients scaled as
  `A_i c_i / p`.

Configuration:

```text
OPENFHE_CCFA_LINEAR_TERMS=128
OPENFHE_CCFA_LINEAR_SLOTS=1024
OPENFHE_CCFA_LINEAR_TRIALS=500
OPENFHE_CCFA_LINEAR_KEEP_PROB=0.90
```

Result file:

```text
results/ccs2026_skip_mean/linear_skip_mean_128term_500trial_p090.csv
```

| mode | trials | mean active terms | mean skipped terms | mean latency | speedup vs deterministic | mean signed error | 95% CI for signed error | mean absolute error |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| deterministic | 1 | 128.000 | 0.000 | 9.875 ms | 0.00% | 2.48e-12 | - | 5.13e-11 |
| unscaled skip | 500 | 115.216 | 12.784 | 8.537 ms | 13.54% | -3.526 | [-3.613, -3.438] | 3.526 |
| mean-restored skip | 500 | 115.216 | 12.784 | 8.519 ms | 13.73% | 0.0115 | [-0.0858, 0.1088] | 1.037 |

This is the defensible speed example for the paper's Layer 1 claim:
coefficient skipping removes about 10% of scalar multiplications and gives a
13.7% latency reduction in this isolated OpenFHE linear-sum path. Without
compensation, the positive linear sum is biased downward. With `A_i c_i / p`,
the signed error is statistically centered at zero, so the expectation is
restored while still skipping coefficients.

Important limitation: this is a linear coefficient-skip benchmark, not the
ResNet bootstrap residual path. The correct wording is that randomization can
provide coefficient-level speedups in tolerant linear/PS scalar-multiplication
paths, while the current ResNet parameters are too sensitive to support the
same claim.
