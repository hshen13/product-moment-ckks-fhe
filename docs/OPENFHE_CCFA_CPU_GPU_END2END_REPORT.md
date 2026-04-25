# OPENFHE CCFA: CPU/GPU and End-to-End Report

## Scope

This report consolidates the main empirical results for the CCFA line across:

- kernel / primitive level
- bootstrap stage and runtime level
- CPU end-to-end downstream tasks
- GPU backend experiments
- significance and failure-boundary analysis

It is intended as a single reference for:

- what was changed
- what was tested
- which results are positive
- which results are null or negative
- what is statistically established

## Code Paths

### CPU mainline

Core modified code:

- [ckksrns-advancedshe.cpp](/root/.local/mprr-openfhe/src/openfhe-development/src/pke/lib/scheme/ckksrns/ckksrns-advancedshe.cpp)
- [ckksrns-fhe.cpp](/root/.local/mprr-openfhe/src/openfhe-development/src/pke/lib/scheme/ckksrns/ckksrns-fhe.cpp)

Main CPU benchmarks:

- [ccfa-full-bootstrap-structure-bench.cpp](/root/.local/mprr-openfhe/src/openfhe-development/src/pke/examples/ccfa-full-bootstrap-structure-bench.cpp)
- [ccfa-he-logreg-bench.cpp](/root/.local/mprr-openfhe/src/openfhe-development/src/pke/examples/ccfa-he-logreg-bench.cpp)
- [ccfa-he-mlp-bench.cpp](/root/.local/mprr-openfhe/src/openfhe-development/src/pke/examples/ccfa-he-mlp-bench.cpp)

### GPU backend

GPU fork:

- [openfhe-gpu-public](/workspace/openfhe-gpu-public)

Main GPU benchmark:

- [ccfa-he-logreg-bench.cpp](/workspace/openfhe-gpu-public/src/pke/examples/ccfa-he-logreg-bench.cpp)

Important GPU-specific additions:

- `OPENFHE_CCFA_BOOT_SLOTS`
- `OPENFHE_CCFA_BOOT_LEVEL_BUDGET0`
- `OPENFHE_CCFA_BOOT_LEVEL_BUDGET1`
- sparse-slot alignment fix:
  - plaintext encoding uses `rt.slots`
  - ciphertexts explicitly call `SetSlots(rt.slots)`

This removed the earlier GPU sparse-slot mismatch:

- `Precomputations for 2048 slots were not generated`

and made it possible to search the actual GPU bootstrap stable zone.

## 1. Kernel / Primitive Level

### 1.1 Delayed execution for linear transforms

Summary file:

- [nc_lineartransform_sweep_summary.csv](/workspace/results/nc_lineartransform_sweep_summary.csv)

Results:

- all tested slot counts have `MAE = 0`
- `delayed` is consistently faster than `generator`

Representative speedups:

- `32` slots: `450.58 ms -> 325.93 ms` (`1.38x`)
- `64` slots: `1048.17 ms -> 640.39 ms` (`1.64x`)
- `128` slots: `1131.72 ms -> 715.72 ms` (`1.58x`)
- `256` slots: `2868.51 ms -> 1547.61 ms` (`1.85x`)
- `512` slots: `6203.05 ms -> 3052.42 ms` (`2.03x`)
- `1024` slots: `7525.68 ms -> 4183.02 ms` (`1.80x`)

Conclusion:

- the delayed linear-transform runtime result is mature
- it is exact and broadly positive

### 1.2 Degree comparison at kernel level

Summary:

- [openfhe_ccfa_degree_comparison_summary.csv](/workspace/results/openfhe_ccfa_degree_comparison_summary.csv)

Key finding:

- increasing deterministic approximation degree does not act like a substitute for product-coupled randomization

Example:

- deterministic degree `59 -> 119`
- latency increases by about `23%`
- deterministic `product_error` changes only marginally:
  - `0.037858 -> 0.037815`

Conclusion:

- simply increasing deterministic degree is not a high-leverage answer to the product-structure problem

## 2. Stage-Level / Assumption Checks

### 2.1 Tail-gated stable randomized full-bootstrap region

Summary:

- [openfhe_ccfa_tailgated_summary.csv](/workspace/results/openfhe_ccfa_tailgated_summary.csv)

Results:

- `18/18` success in the stable tail-gated region
- all tested points are finite
- `mae_vs_input` remains in the `4e-06` to `7e-06` range

Conclusion:

- there is a real stable randomized full-bootstrap regime
- this is a strong systems result independent of end-to-end product-error claims

### 2.2 Block orthogonality

Result:

- [openfhe_ccfa_block_orthogonality.csv](/workspace/results/openfhe_ccfa_block_orthogonality.csv)

Measured:

- `mean_abs_offdiag_corr = 5.49e-02`
- `max_abs_offdiag_corr = 3.40e-01`

Conclusion:

- average cross-block correlation is small
- Assumption 2 is defensible as an average-case decorrelation approximation
- it should not be overstated as strict independence

### 2.3 Shell amplification

Result:

- [openfhe_ccfa_shell_lipschitz_light.csv](/workspace/results/openfhe_ccfa_shell_lipschitz_light.csv)

Measured:

- `CoeffToSlot mean_gain = 3.18e+02`
- `SlotToCoeff mean_gain = 2.90e-05`

Conclusion:

- shell amplification is concentrated in `CoeffToSlot`
- this matches the earlier stage-boundary diagnosis

## 3. CPU End-to-End Results

### 3.1 Mild full-bootstrap benchmark: final answer is negative

Main result:

- [openfhe_ccfa_aggr_p3m3_200seed_stats.csv](/workspace/results/openfhe_ccfa_aggr_p3m3_200seed_stats.csv)

`n=200` paired result:

- `product_error_delta = -1.03e-07`
- `win_rate = 0.39`
- one-sided Wilcoxon `p = 0.9993`

- `cross_error_delta = -8.28e-12`
- `win_rate = 0.365`
- `p = 0.99999`

Interpretation:

- the earlier `50`-seed positive trend was noise
- in the mild end-to-end benchmark, `product_safe` is not better than deterministic
- this is not merely "underpowered"; the direction flips

This is consistent with a shell-attenuation story:

- kernel advantages can be washed out before they reach downstream task metrics

### 3.2 Real downstream HE benchmark: robustness gap

Files:

- [openfhe_ccfa_he_mlp_summary_10seed.csv](/workspace/results/openfhe_ccfa_he_mlp_summary_10seed.csv)
- [openfhe_ccfa_he_mlp_independent_3seed.csv](/workspace/results/openfhe_ccfa_he_mlp_independent_3seed.csv)

Results:

- deterministic:
  - `10/10` success
  - `logit_mae = 3.4288e-06`
  - `logit_mse = 2.7997e-11`
  - accuracy `1.0`
- `product_safe`:
  - `10/10` success
  - `logit_mae = 2.9745e-06`
  - `logit_mse = 1.8050e-11`
  - accuracy `1.0`
- independent:
  - `0/3` success
  - all three runs decode fail

Relative to deterministic:

- `product_safe` improves `logit_mae` by about `13.25%`
- `product_safe` improves `logit_mse` by about `35.53%`
- accuracy is unchanged

Conclusion:

- the robustness gap is real
- `product_safe` and deterministic survive
- independent randomization does not

### 3.3 CPU tuned `k=2` repeated-multiplication downstream task

Files:

- [openfhe_ccfa_he_logreg_k2_tuned_partial_summary.csv](/workspace/results/openfhe_ccfa_he_logreg_k2_tuned_partial_summary.csv)
- [openfhe_ccfa_he_logreg_k2_tuned_partial_stats.csv](/workspace/results/openfhe_ccfa_he_logreg_k2_tuned_partial_stats.csv)

`10`-seed partial summary:

- deterministic:
  - latency `130109.20 ms`
  - `logit_mae = 6.8277e-07`
  - `logit_mse = 9.0912e-13`
  - accuracy `0.60`
- `product_safe`:
  - latency `128283.46 ms`
  - `logit_mae = 5.4554e-07`
  - `logit_mse = 8.4804e-13`
  - accuracy `0.60`

Relative difference:

- `logit_mae` improves by about `20.1%`
- `logit_mse` is slightly better
- latency is slightly better
- accuracy is unchanged

Significance:

- `logit_mae`: `p = 0.09668`
- `logit_mse`: `p = 0.34766`
- latency: `p = 0.1875`

Conclusion:

- on interaction-dominant repeated-multiplication tasks, CPU `product_safe` begins to beat deterministic on downstream fidelity
- this remains trend-level rather than significant at this sample size

## 4. GPU Backend Results

## 4.1 GPU mild structure benchmark

Summary:

- [gpu_ccfa_structure_50seed_summary.csv](/workspace/results/gpu_ccfa_structure_50seed_summary.csv)

`50`-seed result:

- latency:
  - default `1506.83 ms`
  - `product_safe = 1447.93 ms`
  - relative improvement about `3.91%`
  - `p = 0.0717`
- `product_error`:
  - default `3.27e-07`
  - `product_safe = 3.43e-07`
  - `p = 0.7204`
- `cross_error`:
  - default `2.86e-12`
  - `product_safe = 3.18e-12`
  - `p = 0.9156`

Conclusion:

- GPU mild structure has a latency trend, but not a significant one at `50` seeds
- fidelity metrics do not improve

### 4.2 GPU stable zone discovery

The first actual GPU stable zone for the downstream task was:

- `ring_dim = 65536`
- `slots = 8`
- `levelBudget = {3,3}`
- `levelsAfter = 30`
- `productPower = 2`

Key deterministic working points:

- [r65536_s8_l30_k1.csv](/workspace/results/r65536_s8_l30_k1.csv)
- [r65536_s8_l30_k2.csv](/workspace/results/r65536_s8_l30_k2.csv)

This was a major finding:

- the GPU backend needed to be aligned with the official example style
- large ring dimension
- sparse slots
- moderate levels-after

Before sparse-slot alignment fixes, the GPU benchmark failed earlier with slot-precomputation mismatch.

### 4.3 GPU stable-zone retuning

Single-seed retune summary:

- [gpu_stablezone_k2_retune_summary.csv](/workspace/results/gpu_stablezone_k2_retune_summary.csv)

Single-seed best point:

- `kp90_ms990_h6`
- latency `51488.85 ms`
- `logit_mae = 1.3972e-06`
- `logit_mse = 2.4640e-12`

Relative to the stable-zone deterministic seed-1 baseline:

- `logit_mae` improves about `7.0%`
- `logit_mse` improves about `48.4%`
- latency improves about `54.9%`

But this did not remain stable once expanded.

### 4.4 GPU stable-zone near-tie point

Three-seed validation:

- [gpu_stablezone_k2_best3_summary.csv](/workspace/results/gpu_stablezone_k2_best3_summary.csv)
- [gpu_stablezone_k2_best3_stats.csv](/workspace/results/gpu_stablezone_k2_best3_stats.csv)

Result:

- deterministic and retuned `product_safe` become approximately tied
- latency slightly favors `product_safe`
- `logit_mse` slightly favors `product_safe`
- `logit_mae` is essentially tied
- nothing is significant

This was the first indication that GPU retuning could at least pull `product_safe` back from clearly losing.

### 4.5 GPU stable-zone `cand_k`: the first significant GPU result

Best stable-zone candidate:

- `keep_prob = 0.91`
- `min_scale = 0.992`
- `protect_head = protect_tail = 6`
- `eligible_rel_abs = 0.005`

Files:

- [gpu_stablezone_k2_candk_10seed_summary.csv](/workspace/results/gpu_stablezone_k2_candk_10seed_summary.csv)
- [gpu_stablezone_k2_candk_10seed_stats.csv](/workspace/results/gpu_stablezone_k2_candk_10seed_stats.csv)

`10`-seed end-to-end GPU result:

- deterministic:
  - success rate `1.0`
  - latency `60271.74 ms`
  - `logit_mae = 1.86188e-06`
  - `logit_mse = 6.88065e-12`
  - accuracy `0.60`
- `product_safe`:
  - success rate `1.0`
  - latency `50230.06 ms`
  - `logit_mae = 2.19387e-06`
  - `logit_mse = 9.33288e-12`
  - accuracy `0.60`

Significance:

- latency:
  - mean delta `10041.68 ms`
  - win rate `0.8`
  - **Wilcoxon `p = 0.01367`**
- `logit_mae`:
  - `p = 0.9580`
- `logit_mse`:
  - `p = 0.9199`
- accuracy:
  - identical

Interpretation:

- this is the first statistically significant GPU positive result
- the positive signal is **end-to-end latency**
- not fidelity

In plain terms:

- `product_safe` is about `16.7%` faster
- with unchanged accuracy
- but with worse logit-level fidelity

## 5. What Is Positive, Null, and Negative

### Strong positive results

1. Delayed linear transforms
- exact
- broad and robust speedup

2. Stable randomized full-bootstrap region
- `18/18` success

3. CPU downstream robustness gap
- deterministic and `product_safe` survive
- independent fails

4. CPU tuned `k=2` downstream trend
- `product_safe` improves `logit_mae` by about `20%`

5. GPU stable-zone latency win
- `cand_k`
- `10` seeds
- significant latency improvement

### Null or near-null results

1. GPU mild structure latency at `50` seeds
- positive direction
- not significant

2. GPU stable-zone fidelity after retuning
- some 3-seed points improve
- not stable enough to remain positive once sample count increases

### Negative results

1. CPU mild full-bootstrap benchmark
- `n=200`
- deterministic is better

2. Many GPU retune points
- single-seed positives often disappear at `5` or `10` seeds

3. GPU `k=3`
- all modes decode fail

## 6. Final Interpretation

The evidence now supports a layered story rather than a uniform "everywhere better" story.

### CPU side

- kernel and runtime-level results are strong
- there is a stable randomized bootstrap regime
- mild end-to-end product-error claims do not hold up
- the real positive CPU task-level evidence appears only on interaction-dominant downstream tasks

### GPU side

- the backend required dedicated alignment and retuning
- sparse-slot / official-like bootstrap settings were essential
- the first stable GPU positive result is **latency**, not fidelity
- once properly retuned, `product_safe` can be significantly faster end-to-end without harming final accuracy
- but it does not currently improve downstream logit fidelity on GPU

## 7. Bottom Line

The most defensible cross-platform summary is:

1. `product_safe` is **not universally better** than deterministic.
2. On CPU, its downstream value appears in:
- robustness against independent randomization failure
- interaction-dominant repeated-multiplication tasks
3. On GPU, its strongest current value is:
- **significant end-to-end latency improvement**
- with unchanged final task accuracy
- but not improved logit-level fidelity

If this report is used in a paper, the safest concise claim is:

> Product-coupled randomization defines a real and usable design point. On CPU it improves robustness and can improve downstream fidelity in interaction-dominant settings; on GPU, after backend-specific retuning, it yields a significant end-to-end latency reduction without degrading final classification accuracy.
