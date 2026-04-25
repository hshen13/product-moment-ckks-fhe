# CCS 2026 Layered Experiment Results

Generated on 2026-04-13 from the patched OpenFHE tree at
`/workspace/openfhe-development-v142-min`.

## Layer 1: Why Randomize?

### L1.1 OpenFHE advanced-ckks-style bootstrap

Benchmark:

- binary: `ccfa-randomization-speed-bench`
- source configuration: OpenFHE `advanced-ckks-bootstrapping` style sparse setup
- ring dimension: 4096
- slots: 8
- level budget: `{3,3}`
- randomized setting: Bernoulli p=0.50, protect head/tail 16 coefficients
- result CSV: `/workspace/results/ccs2026_layered/l1_randomization_speed.csv`

| variant | success | mean latency (ms) | speedup vs det | faster pairs | sign-test p | mean MAE |
|---|---:|---:|---:|---:|---:|---:|
| deterministic | 15/15 | 843.19 | reference | - | - | 4.92e-08 |
| independent p=0.50 | 15/15 | 841.77 | 0.17% | 9/15 | 0.607 | 1.70e-02 |
| product-coupled p=0.50 | 14/15 | 843.02 | 0.01% | 6/15 | 0.607 | 2.14e-02 |

Result: this official sparse 8-slot benchmark does **not** satisfy the
planned `>=7%` latency-reduction criterion. Aggressive p=0.50 without
tail protection decodes incorrectly; tail protection restores most runs but
removes almost all exploitable work.

### L1.2 Zero-skip profiling

Instrumentation added in `ckksrns-advancedshe.cpp`:

- profile columns: `candidate_terms`, `nonzero_terms`, `skipped_terms`
- profiled regions: `ccfa_randomizer`, `linear_wsum`, `chebyshev_linear`
- evaluator change: `EvalPartialLinearWSum` now skips zero coefficients instead
  of multiplying by zero.

Official advanced-style aggregate:

| variant | randomizer candidates | zeroed by randomizer | sparse linear candidates | skipped sparse-linear terms |
|---|---:|---:|---:|---:|
| deterministic | 0 | 0 | 1110 | 15 |
| independent p=0.50 | 45 | 29 | 1105 | 15 |
| product-coupled p=0.50 | 45 | 24 | 1106 | 15 |

Result: the official sparse bootstrap does zero some randomized tail terms, but
the randomized zeros mostly do not reach the sparse linear sums that dominate
latency. This explains why L1.1 does not show the expected speedup.

### L1 speed regime that does pass

Benchmark:

- binary: `ccfa-transformer-inner-product-bench`
- task: encrypted 768-dimensional inner product
- pairs: 50
- keep probability: p=0.10
- result CSV: `/workspace/results/ccs2026_layered/l3_inner_product_p010_50pair.csv`

| variant | mean latency (ms) | speedup vs det | faster pairs | sign-test p | mean abs error | p95 abs error |
|---|---:|---:|---:|---:|---:|---:|
| deterministic | 289.47 | reference | - | - | 0 | 0 |
| independent p=0.10 | 270.56 | 6.46% | 47/50 | 3.71e-11 | 2793.61 | 16238.41 |
| product-coupled p=0.10 | 268.60 | 7.16% | 50/50 | 1.78e-15 | 830.65 | 984.00 |

Result: randomization has a real compute advantage in transformer-scale
product workloads. Product-coupled keeps the speed advantage while reducing
mean absolute product error to 0.297x independent and p95 absolute error to
0.061x independent.

## Layer 2: Why Control First Moments?

The ResNet-20 third-party code from Lee et al. was downloaded and compiled:

- repo path: `/workspace/LowMemoryFHEResNet20`
- deterministic single-image smoke: success, prediction `Cat`, whole-circuit
  time about 405 s
- product-safe single-image smoke: success, prediction `Cat`, whole-circuit
  time about 363 s
- independent p=0.91 single-image smoke: success, prediction `Cat`, whole-
  circuit time about 397 s
- report: `/workspace/results/ccs2026_resnet/E8_RESNET20_SMOKE_RESULTS.md`

Result: this is only a smoke test, not a CIFAR-10 accuracy table. It proves the
third-party OpenFHE ResNet path runs with the CCFA environment knobs. It does
not yet establish the planned `>=3%` independent accuracy drop or `<=1%`
product-coupled drop.

## Layer 3: Why Control Second Moments?

### L3.1 Transformer-scale encrypted inner product

Primary p=0.50 run:

- result CSV: `/workspace/results/ccs2026_layered/l3_inner_product_sparse_eval.csv`
- pairs: 100
- dimension: 768

| variant | mean latency (ms) | speedup vs det | mean abs error | mean rel error | p95 abs error | p95 rel error |
|---|---:|---:|---:|---:|---:|---:|
| deterministic | 285.35 | reference | 0 | 0 | 0 | 0 |
| independent p=0.50 | 280.96 | 1.48% | 1691.05 | 37.49 | 3999.12 | 94.87 |
| product-coupled p=0.50 | 279.01 | 2.16% | 652.31 | 14.26 | 881.98 | 20.27 |

Result: at p=0.50, product-coupled reduces mean absolute inner-product error
to 0.386x independent and p95 absolute error to 0.221x independent. Pairwise
absolute-error wins are 74/100, sign-test p=1.67e-06.

### L3.2 BERT-style attention score

Benchmark:

- binary: `ccfa-bert-attention-bench`
- result CSV: `/workspace/results/ccs2026_layered/l3_bert_attention_sparse_eval.csv`
- tokens: 8
- hidden dimension: 768
- seeds: 5
- rows: 40 attention rows per randomized variant

| variant | mean latency (ms) | score MAE | score max abs | softmax MAE | top-1 match |
|---|---:|---:|---:|---:|---:|
| deterministic | 4035.97 | 0 | 0 | 0 | 1.00 |
| independent p=0.60 | 4200.93 | 12.077 | 36.375 | 0.1731 | 1.00 |
| product-coupled p=0.60 | 4134.43 | 10.966 | 31.479 | 0.1724 | 1.00 |

Result: product-coupled improves attention-score MAE to 0.908x independent.
The effect is directionally consistent but not strong enough for the stricter
planned `<=0.3x` pass criterion in this synthetic 8-token setup. Top-1 remains
unchanged for all rows, so this setup is not decision-boundary sensitive.

## Third-party Benchmark Status

| benchmark | repo path | status |
|---|---|---|
| Lee et al. ResNet-20 | `/workspace/LowMemoryFHEResNet20` | compiled and smoke-tested |
| NEXUS | `/workspace/NEXUS` | downloaded; modified SEAL with ZSTD built; `bin/main` and `bin/bootstrapping` compile |
| BOLT | `/workspace/BOLT`, `/workspace/EzPC-BOLT-bert` | downloaded; implementation is EzPC/SCI, not OpenFHE |
| THOR | not found | no reliable public code repository found in search |

NEXUS and BOLT are not OpenFHE overlays. They require a SEAL/EzPC port of the
CCFA randomization mechanism before they can be used for apples-to-apples
product-coupled runs.

## Bottom Line

- The original L1.1 official sparse OpenFHE speed claim does not pass.
- Randomization itself is still justified, but the passing regime is not the
  8-slot official bootstrap; it is transformer-scale product computation.
- Product-coupled is effective where the paper should focus: product-sensitive
  workloads. The strongest current table is L3.1 inner product at p=0.50 for
  error significance and p=0.10 for speed/error tradeoff.
- The full ResNet-20 CIFAR-10 and NEXUS/BOLT/THOR tables still require porting
  or long-running third-party workloads; they are not completed by this OpenFHE
  overlay alone.
