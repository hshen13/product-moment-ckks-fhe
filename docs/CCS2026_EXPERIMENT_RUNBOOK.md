# CCS 2026 Experiment Runbook

This package is a source overlay, not a standalone OpenFHE checkout. Copy `cpu_openfhe/` into a full CPU OpenFHE tree or `gpu_openfhe/` into a full GPU fork, then rebuild examples.

## Build

```bash
cp -r openfhe_ccfa_githubready/gpu_openfhe/* /path/to/openfhe-gpu-public/
cd /path/to/openfhe-gpu-public
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build -j
```

Set the example binary directory:

```bash
export OPENFHE_EXAMPLE_BIN_DIR=/path/to/openfhe-gpu-public/build/bin/examples/pke
```

## Smoke Run

Use a smoke run first. It reduces seed counts and Lipschitz trials while exercising the new code paths.

```bash
CCS2026_SMOKE=1 CCS2026_EXPERIMENTS=E3,E4,E5,E6,E7,E9 \
  openfhe_ccfa_githubready/scripts/run_ccs2026_experiments.sh
```

## Full Run

```bash
openfhe_ccfa_githubready/scripts/run_ccs2026_experiments.sh
```

By default this runs:

- `E1`: dense MLP2 packing, `N=2^16`, `slots=32768`
- `E2`: encrypted depth scaling at depths `2,4,6,8`
- `E3`: deterministic degree `59` vs `119` and product-coupled degree `59`
- `E4`: Bernoulli/Gaussian/Uniform distribution ablation
- `E5`: independent/within-level/product-coupled/fully-coupled ablation
- `E6`: SOTA bootstrap composition: default Chebyshev, OpenFHE Meta-BTS, OpenFHE composite scaling, and composite+Meta-BTS, each with and without product-coupled
- `E7`: end-to-end bootstrap bias grid
- `E8`: failure-rate sweep over keep probabilities
- `E9`: shell Lipschitz probe with mean/max/p95

The output directory defaults to `/workspace/results/ccs2026`. Override it with:

```bash
export CCS2026_RESULT_DIR=/path/to/results
```

## Product-Sensitive Layer-3 Run

The final CCS narrative should use product-sensitive workloads rather than pointwise bootstrap precision as the main axis. Run the transformer-scale benchmarks with:

```bash
openfhe_ccfa_githubready/scripts/run_product_sensitive_experiments.sh
```

This produces:

| Experiment | Primary CSV |
|---|---|
| E9 BERT-style single-head `QK^T` attention | `e9_bert_attention.csv` |
| E10 transformer-scale encrypted inner product | `e10_inner_product.csv` |

Defaults:

- E9: `tokens=8`, `dim=768`, `degree=59`, `keep_prob=0.60`, correlated Q/K
- E10: `pairs=100`, `dim=768`, `degree=59`, `keep_prob=0.50`, correlated pairs `y=x+noise`

Use `CCS2026_SMOKE=1` for a short validation run.

## Layered CCS 2026 Run

The layered runner exercises the current paper narrative:

```bash
openfhe_ccfa_githubready/scripts/run_layered_ccs2026_experiments.sh
```

By default it runs:

- `L1`: OpenFHE `advanced-ckks-bootstrapping`-style randomization speed test
- `E9`: BERT-style single-head `QK^T` attention
- `E10`: transformer-scale encrypted inner product

To include the stronger transformer speed regime at p=0.10:

```bash
CCS2026_LAYERED_EXPERIMENTS=L1,E9,E10,E10_SPEED \
  openfhe_ccfa_githubready/scripts/run_layered_ccs2026_experiments.sh
```

The main result report is:

```text
docs/CCS2026_LAYERED_EXPERIMENT_RESULTS.md
```

The follow-up completion report for the requested 10-seed runs is:

```text
docs/CCS2026_COMPLETION_RESULTS.md
```

Important interpretation: the official sparse 8-slot OpenFHE bootstrap does
not currently show the planned 7% speedup. The 7% randomized speed advantage
appears in the transformer-scale inner-product regime at p=0.10, where
product-coupled also reduces product error relative to independent
randomization.

## New Runtime Knobs

The randomized PS kernel now supports:

- `OPENFHE_CCFA_DIST=bernoulli|gaussian|uniform`
- `OPENFHE_CCFA_COUPLING=independent|within_level|product_coupled|fully`

Legacy settings still work:

- `OPENFHE_CCFA_MODE=none`
- `OPENFHE_CCFA_MODE=independent`
- `OPENFHE_CCFA_MODE=product`
- `OPENFHE_CCFA_MODE=product_safe`

When `OPENFHE_CCFA_COUPLING` is unset, `product` and `product_safe` select `product_coupled`; other randomized modes select `independent`.

## Result Mapping

| Experiment | Primary CSV |
|---|---|
| E1 dense packing | `e1_dense_det.csv`, `e1_dense_product_safe.csv`, `e1_dense_noise_flood.csv` |
| E2 depth scaling | `e2_multi_layer.csv` |
| E3 degree comparison | `e3_degree59.csv`, `e3_degree119.csv` |
| E4 distribution ablation | `e4_distribution_*.csv` |
| E5 coupling ablation | `e5_coupling_*.csv` |
| E6 SOTA bootstrap composition | `e6_sota_bootstrap.csv` |
| E7 bias | `e7_bias.csv` |
| E8 bound sweep | `e8_bound_p*.csv` |
| E9 Lipschitz | `e9_lipschitz.csv` |
| Markdown summary | `ccs2026_experiment_summary.md` |

## Notes on E6, PaCo, Minimax, and External Transformer Baselines

The runner now completes the OpenFHE-reproducible E6 path automatically. It compares product-coupled against:

- OpenFHE default Chebyshev bootstrap
- OpenFHE Meta-BTS two-iteration bootstrap
- OpenFHE composite scaling, used as the MinBoot-style composite-polynomial baseline available in OpenFHE
- OpenFHE composite scaling plus Meta-BTS

PaCo is not an OpenFHE backend. The public proof-of-concept implementation is SageMath-based and can be downloaded separately, but it requires a `sage` executable and does not plug into this overlay's C++ benchmark harness. Lee-style minimax coefficients are also not exposed as an OpenFHE runtime mode in this checkout. Do not label the OpenFHE composite row as the original MinBoot implementation; label it as the OpenFHE composite-scaling / MinBoot-style baseline.

The OpenFHE overlay now includes E10 as a transformer-scale encrypted inner
product benchmark. NEXUS and BOLT have been downloaded separately:

- `/workspace/NEXUS`
- `/workspace/BOLT`
- `/workspace/EzPC-BOLT-bert`

NEXUS builds after installing NTL/GMP and compiling its bundled SEAL 4.1 with
ZSTD support. BOLT points to an EzPC/SCI implementation. These systems are not
OpenFHE overlays, so product-coupled CCFA requires a SEAL/EzPC port before
they can be used as apples-to-apples randomized baselines.

To export a small CIFAR-10 subset for the third-party ResNet-20 code:

```bash
sudo apt-get install -y python3-pil
openfhe_ccfa_githubready/scripts/export_cifar10_subset.py
```

This writes one test image per CIFAR-10 class to
`/workspace/LowMemoryFHEResNet20/inputs/cifar10_subset/`.
