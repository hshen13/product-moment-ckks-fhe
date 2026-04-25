# CCS 2026 Completion Results

Generated on 2026-04-13. Result directory:
`/workspace/results/ccs2026_completion`.

## What Was Completed

| item | status | primary output |
|---|---|---|
| Dense packing, N=2^16, slots=32768 | completed, 10 seeds | `e1_dense_*_10seed.csv` |
| Multi-layer encrypted MLP depth 2/4/6/8 | completed at moderate ring, 10 seeds | `e2_multi_layer_10seed_moderate.csv` |
| Production-size multi-layer depth | partial only | `e2_multi_layer_N65536_partial.csv` |
| Higher-degree deterministic baseline | completed, 10 randomized seeds | `e3_degree59.csv`, `e3_degree119.csv` |
| Distribution ablation | completed, 10 seeds | `e4_distribution_*.csv` |
| Coupling ablation | completed, 10 seeds | `e5_coupling_*.csv` |
| BERT-style attention | completed, 10 seeds | `e9_bert_attention_10seed.csv` |
| ResNet-20 CIFAR-10 | CIFAR subset exported; one CIFAR image smoke-tested | `resnet_cifar10_cat_smoke.csv` |
| NEXUS | downloaded and compiled | `/workspace/NEXUS/build-zstd/bin/*` |
| BOLT | downloaded | `/workspace/BOLT`, `/workspace/EzPC-BOLT-bert` |

## Dense Packing

Configuration: `N=65536`, `slots=32768`, 10 data seeds.

| method | success | mean latency | accuracy | logit MAE |
|---|---:|---:|---:|---:|
| deterministic | 10/10 | 58.25 s | 0.999924 | 2.758e-05 |
| product-safe | 10/10 | 57.53 s | 0.999908 | 2.761e-05 |
| noise-flood sigma=1e-6 | 10/10 | 56.32 s | 0.999899 | 2.760e-05 |

Product-safe is 1.22% faster than deterministic on average, but only wins
7/10 paired seeds, so this is not a strong latency claim. The important
claim is correctness at dense packing: product-safe matches deterministic
accuracy and error scale at full SIMD packing.

## Multi-Layer Depth Scaling

Completed 10 seeds at `N=4096`, `slots=1024`, depths `2/4/6/8`. A production
`N=65536` run was started and preserved, but full 10 seeds would take hours.

| depth | deterministic success | independent success | product-safe success | independent accuracy on successful runs | product-safe accuracy |
|---:|---:|---:|---:|---:|---:|
| 2 | 10/10 | 3/10 | 10/10 | 0.475 | 1.000 |
| 4 | 10/10 | 3/10 | 10/10 | 0.477 | 1.000 |
| 6 | 10/10 | 2/10 | 10/10 | 0.527 | 1.000 |
| 8 | 10/10 | 2/10 | 10/10 | 0.502 | 1.000 |

This supports the depth-scaling claim: independent randomization is unstable
even when it decrypts; product-safe tracks deterministic through depth 8.

## Higher-Degree Baseline

| method | degree | success | mean product error | mean latency |
|---|---:|---:|---:|---:|
| deterministic | 59 | 1/1 | 0.0379 | 118.83 ms |
| independent | 59 | 10/10 | 1.3451 | 95.30 ms |
| product | 59 | 10/10 | 0.8275 | 91.86 ms |
| deterministic | 119 | 1/1 | 0.0378 | 225.75 ms |
| independent | 119 | 10/10 | 0.3864 | 150.81 ms |
| product | 119 | 10/10 | 0.2211 | 144.26 ms |

Degree 119 improves randomized error, but roughly doubles deterministic
latency. Product-coupled remains better than independent at both degrees.

## Distribution Ablation

| distribution | product error |
|---|---:|
| Bernoulli | 0.0910 |
| Gaussian | 0.0916 |
| Uniform | 0.0650 |

This empirical benchmark does not prove Bernoulli is best by raw error. The
Bernoulli argument should be stated as a structural theorem: Bernoulli gives
the exact selector identity `A^2=A`, which is what yields closed-form
second-moment control. The distribution ablation should not be oversold.

## Coupling Ablation

| coupling | product error |
|---|---:|
| independent | 0.1719 |
| within-level | 0.1646 |
| product-coupled | 0.0910 |
| fully coupled | 0.1846 |

Product-coupled is the best coupling pattern in this 10-seed ablation.

## BERT-Style Attention

Configuration: 10 seeds, 8 tokens, hidden dimension 768, 80 attention rows per
randomized method.

| method | score MAE | score max abs | softmax MAE | top-1 match |
|---|---:|---:|---:|---:|
| deterministic | 0 | 0 | 0 | 1.000 |
| independent | 10.160 | 30.741 | 0.1732 | 1.000 |
| product-coupled | 10.056 | 20.384 | 0.1728 | 1.000 |

The 10-seed result is weaker than the earlier 5-seed result. Product-coupled
does not significantly improve mean score MAE here (`0.990x`, 39/80 wins).
It does reduce max score error (`0.663x`) and slightly improves softmax MAE
(50/80 wins, sign-test p=0.033), but top-1 is unchanged. This should be used
as supporting evidence, not as the main transformer result.

## ResNet-20 CIFAR Smoke

CIFAR-10 test batch was downloaded and one image per class was exported to:

```text
/workspace/LowMemoryFHEResNet20/inputs/cifar10_subset
```

One true CIFAR test image was run through the encrypted ResNet-20 path:

| image | true class | method | predicted class | correct | logit MAE vs deterministic |
|---|---|---|---|---:|---:|
| `00_cat_testidx0.png` | cat | deterministic | cat | 1 | 0 |
| `00_cat_testidx0.png` | cat | product-safe | cat | 1 | 0.0143 |
| `00_cat_testidx0.png` | cat | independent | cat | 1 | 0.0122 |

This is a real CIFAR image smoke test, not a CIFAR-10 accuracy benchmark. A
10-image encrypted subset would require roughly 3 hours for three methods on
this machine; full CIFAR-10 is not practical in this turn.

## NEXUS/BOLT Status

NEXUS was downloaded and compiled after building its bundled SEAL 4.1 with
ZSTD and installing NTL/GMP. The binaries exist at:

```text
/workspace/NEXUS/build-zstd/bin/main
/workspace/NEXUS/build-zstd/bin/bootstrapping
```

A 120-second smoke run of `bin/main` entered ciphertext expansion and did not
finish. More importantly, NEXUS is a modified-SEAL system, not OpenFHE. The
CCFA randomization mechanism has not been ported to its SEAL bootstrapper, so
NEXUS cannot yet produce deterministic/independent/product-coupled comparison
rows.

BOLT was downloaded, but its implementation points to EzPC/SCI and is also
not an OpenFHE overlay. It likewise requires a separate port.

## Decision

The five requested gaps are now partially closed:

- Dense packing: closed.
- Depth scaling: closed at moderate ring; production-size partial preserved.
- Distribution/coupling/higher-degree: closed with 10 seeds.
- BERT attention: closed with 10 seeds, but result is weak and should not be
  the main transformer claim.
- ResNet-20: only CIFAR smoke is closed; full CIFAR-10/subset accuracy remains
  expensive and should be treated as future/long-run work unless scheduled.
