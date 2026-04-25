# GPU-Only Latest Results

## Scope

This report summarizes the latest GPU-only experiment block on the compact encrypted two-layer network benchmark. It focuses on:

1. `8-slot` end-to-end three-mode comparison
2. `slots=256` dense fast-profile search
3. GPU timing side-channel evaluation

All experiments use:
- [ccfa-he-mlp2-bench.cpp](/workspace/openfhe-gpu-public/src/pke/examples/ccfa-he-mlp2-bench.cpp)

---

## 1. GPU 8-Slot End-to-End Three-Mode Comparison

### Configuration

- GPU enabled
- `ring_dim = 65536`
- `slots = 8`
- `levelsAfter = 30`
- `levelBudget = {3,3}`
- compact two-layer encrypted network
- seeds `1..10` for `deterministic` and `product_safe`

Modes:
- `deterministic`
- `product_safe`
- `noise_flood`

`product_safe` gate:
- `keep_prob = 0.91`
- `min_scale = 0.992`
- `protect_head = protect_tail = 6`
- `eligible_rel_abs = 0.005`

`noise_flood`:
- added after bootstrap on the bootstrapped feature ciphertexts
- `sigma = 1e-6`

### Result Files

- deterministic:
  - [det.csv](/workspace/results/gpu_mlp2_8slot_10seed/det.csv)
- product_safe:
  - [ps.csv](/workspace/results/gpu_mlp2_8slot_10seed/ps.csv)
- noise_flood:
  - [noise1x.csv](/workspace/results/gpu_mlp2_8slot_10seed/noise1x.csv)

### Mean Results

| Mode | Success | Latency (ms) | h1 MAE | h2 MAE | logit MAE | logit MSE | Accuracy |
|---|---:|---:|---:|---:|---:|---:|---:|
| deterministic | 10/10 | 39218.84 | 2.2128e-06 | 1.6741e-06 | 2.4536e-06 | 1.0346e-11 | 1.0 |
| product_safe | 10/10 | 38754.54 | 1.7822e-06 | 1.2897e-06 | 2.2401e-06 | 9.8225e-12 | 1.0 |
| noise_flood | 0/2 observed | N/A | N/A | N/A | N/A | N/A | N/A |

### Statistical Notes: deterministic vs product_safe

The `10`-seed paired summary is:

| Metric | Wins (`det > ps`) | One-sided Wilcoxon p |
|---|---:|---:|
| latency | 9/10 | 0.00293 |
| h1 MAE | 6/10 | 0.27832 |
| h2 MAE | 7/10 | 0.24609 |
| logit MAE | 6/10 | 0.27832 |
| logit MSE | 6/10 | 0.24609 |

### Interpretation

- `product_safe` is usable and fully successful (`10/10`).
- `noise_flood` is not merely weaker; it remains unusable in this setup (`0/2` observed success, both failed immediately).
- Relative to deterministic, `product_safe` improves all reported mean fidelity metrics:
  - `h1_mae`: `2.21e-06 -> 1.78e-06`
  - `h2_mae`: `1.67e-06 -> 1.29e-06`
  - `logit_mae`: `2.45e-06 -> 2.24e-06`
  - `logit_mse`: `1.03e-11 -> 9.82e-12`
- Accuracy is unchanged (`1.0` vs `1.0`).
- End-to-end latency is significantly better:
  - `39218.84 ms -> 38754.54 ms`
  - `9/10` wins
  - one-sided Wilcoxon `p = 0.00293`

This supports the claim that coordinated randomization is qualitatively different from naive post-bootstrap randomization, and that on this GPU end-to-end benchmark it can deliver a statistically significant latency gain while preserving accuracy.

---

## 2. GPU Dense Fast Profile: `slots = 256`

Dense packing at `slots=1024` under the original `65536 / 30 / {3,3}` profile was too heavy to be practical. A fast dense profile was therefore introduced:

- `ring_dim = 32768`
- `slots = 256`
- `levelsAfter = 20`
- `levelBudget = {2,2}`
- compact two-layer encrypted network

This profile is the first dense GPU configuration that runs reliably.

### 2.1 Candidate `cand4`

Files:
- deterministic:
  - [det.csv](/workspace/results/gpu_mlp2_slots256_best3/det.csv)
- product_safe:
  - [ps_cand4.csv](/workspace/results/gpu_mlp2_slots256_best3/ps_cand4.csv)

Parameters:
- `keep_prob = 0.9`
- `min_scale = 0.995`
- `protect_head = protect_tail = 5`
- `eligible_rel_abs = 0.005`

#### Mean Results

| Mode | Success | Latency (ms) | h1 MAE | h2 MAE | logit MAE | logit MSE | Accuracy |
|---|---:|---:|---:|---:|---:|---:|---:|
| deterministic | 3/3 | 11280.41 | 2.9523e-06 | 1.0267e-06 | 3.3907e-06 | 1.2074e-11 | 1.0 |
| product_safe (`cand4`) | 3/3 | 10475.49 | 3.1495e-06 | 1.7354e-06 | 3.4295e-06 | 1.3131e-11 | 1.0 |

#### Interpretation

`cand4` is a latency-oriented point:
- average latency improves by about `7.2%`
- task-level fidelity is slightly worse
- accuracy is unchanged

### 2.2 Candidate `cand2`

Files:
- deterministic:
  - [det.csv](/workspace/results/gpu_mlp2_slots256_cand2_best3/det.csv)
- product_safe:
  - [ps_cand2.csv](/workspace/results/gpu_mlp2_slots256_cand2_best3/ps_cand2.csv)

Parameters:
- `keep_prob = 0.98`
- `min_scale = 0.995`
- `protect_head = protect_tail = 7`
- `eligible_rel_abs = 0.005`

#### Mean Results

| Mode | Success | Latency (ms) | h1 MAE | h2 MAE | logit MAE | logit MSE | Accuracy |
|---|---:|---:|---:|---:|---:|---:|---:|
| deterministic | 3/3 | 10874.96 | 7.7835e-06 | 3.0216e-06 | 1.4113e-05 | 3.1114e-10 | 1.0 |
| product_safe (`cand2`) | 3/3 | 11101.14 | 7.7377e-06 | 6.2470e-06 | 1.4297e-05 | 5.3787e-10 | 1.0 |

#### Interpretation

`cand2` is not stable:
- two of three seeds show better `logit_mae`
- but the mean fidelity does not improve
- latency is slightly worse

### 2.3 Candidate `c3`

This was the most promising fidelity-edge point in the local search:
- `keep_prob = 0.99`
- `min_scale = 0.995`
- `protect_head = protect_tail = 7`
- `eligible_rel_abs = 0.005`

5-seed validation files:
- deterministic:
  - [det.csv](/workspace/results/gpu_mlp2_slots256_c3_5seed/det.csv)
- product_safe:
  - [ps_c3.csv](/workspace/results/gpu_mlp2_slots256_c3_5seed/ps_c3.csv)

#### Mean Results

| Mode | Success | Latency (ms) | logit MAE | logit MSE | Accuracy |
|---|---:|---:|---:|---:|---:|
| deterministic | 5/5 | 18691.18 | 6.2049e-06 | 6.1897e-11 | 1.0 |
| product_safe (`c3`) | 5/5 | 18157.73 | 6.8379e-06 | 5.9532e-11 | 1.0 |

#### Statistical Notes

| Metric | Wins (`det > ps`) | One-sided Wilcoxon p |
|---|---:|---:|
| latency | 3/5 | 0.40625 |
| logit MAE | 3/5 | 0.59375 |
| logit MSE | 3/5 | 0.50000 |

#### Interpretation

`c3` does not define a stable positive region:
- latency is slightly better on average
- `logit_mse` is slightly better on average
- `logit_mae` is worse on average
- none of these are significant

### 2.4 Ultra-Conservative Candidate `u3`

The most conservative follow-up point was:
- `keep_prob = 0.999`
- `min_scale = 0.997`
- `protect_head = protect_tail = 8`
- `eligible_rel_abs = 0.005`

5-seed validation files:
- deterministic:
  - [det.csv](/workspace/results/gpu_mlp2_slots256_u3_5seed/det.csv)
- product_safe:
  - [ps_u3.csv](/workspace/results/gpu_mlp2_slots256_u3_5seed/ps_u3.csv)

#### Mean Results

| Mode | Success | Latency (ms) | logit MAE | logit MSE | Accuracy |
|---|---:|---:|---:|---:|---:|
| deterministic | 5/5 | 16561.18 | 9.8655e-06 | 1.5720e-10 | 1.0 |
| product_safe (`u3`) | 5/5 | 18294.78 | 1.1431e-05 | 3.8717e-10 | 1.0 |

#### Statistical Notes

| Metric | Wins (`det > ps`) | One-sided Wilcoxon p |
|---|---:|---:|
| latency | 2/5 | 0.78125 |
| logit MAE | 2/5 | 0.68750 |
| logit MSE | 2/5 | 0.78125 |

#### Interpretation

Even the ultra-conservative region fails to produce a stable fidelity win.

### Dense-Profile Conclusion

The dense `slots=256` fast profile is now fully operational, but no stable positive `product_safe` region has been found:

- some points improve latency
- some points give fragile single-seed or small-sample fidelity wins
- none survive expansion into a defendable multi-seed positive region

The current conclusion is:

> In the present GPU backend and dense fast-profile setting, `product_safe` does not yet exhibit a stable, reproducible advantage over deterministic bootstrapping.

---

## 3. GPU Timing Side-Channel Evaluation

Files:
- script:
  - [run_gpu_mlp2_timing_sidechannel.py](/workspace/run_gpu_mlp2_timing_sidechannel.py)
- per-run rows:
  - [rows.csv](/workspace/results/gpu_mlp2_timing_sidechannel/rows.csv)
- summary:
  - [summary.csv](/workspace/results/gpu_mlp2_timing_sidechannel/summary.csv)
- per-input means:
  - [per_input_means.csv](/workspace/results/gpu_mlp2_timing_sidechannel/per_input_means.csv)

### Configuration

- GPU only
- `8-slot` compact MLP2
- `product_safe`
- `10` distinct input seeds
- `3` repeats per input
- total `30` runs

### Results

- successful runs: `30/30`
- ANOVA:
  - `F = 0.9218`
  - `p = 0.5269`
- mean latency:
  - `39456.97 ms`
- coefficient of variation across per-input mean latencies:
  - `CV = 0.936%`

Per-input mean latency stays in a narrow band:
- minimum mean: `38966.28 ms`
- maximum mean: `40219.31 ms`

### Interpretation

We do not observe statistically significant plaintext-dependent timing variation in this GPU benchmark:

> On the GPU compact two-layer benchmark, `product_safe` gating shows no statistically significant input-dependent timing variation (`ANOVA p = 0.527`), and the cross-input coefficient of variation remains below `1%` (`CV = 0.94%`).

---

## Overall Takeaways

### What the GPU results now support

1. The `8-slot` compact encrypted two-layer network is a working end-to-end GPU benchmark.
2. Coordinated randomization is meaningfully different from naive randomization:
   - `product_safe` runs
   - `noise_flood` fails (`0/2` observed)
3. On the `8-slot` GPU benchmark, `product_safe` provides a statistically significant end-to-end latency win while preserving accuracy.
4. There is no evidence of significant plaintext-dependent timing leakage in the tested GPU `product_safe` configuration.

### What the GPU results do not support

1. A stable, multi-seed fidelity win for `product_safe` in the dense `slots=256` profile.
2. A defendable claim that GPU `product_safe` is consistently better than deterministic across denser packing regimes.

### Best current framing

The strongest GPU story is:

- end-to-end GPU benchmark exists and runs
- `product_safe` is a viable coordinated randomization strategy
- on the `8-slot` compact two-layer benchmark, it yields a significant latency gain while preserving accuracy
- naive post-bootstrap randomization is not viable in the same task
- timing behavior appears input-independent
- but dense-packing fidelity gains are currently unstable
