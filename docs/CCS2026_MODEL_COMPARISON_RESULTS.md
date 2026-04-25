# CCS 2026 Tuned Cross-Model Comparison

Recommended tuned deployment parameters:

```bash
OPENFHE_CCFA_BOOT_KEEP_PROB=0.91
OPENFHE_CCFA_MIN_SCALE=0.992
OPENFHE_CCFA_PROTECT_HEAD=8
OPENFHE_CCFA_PROTECT_TAIL=8
OPENFHE_CCFA_ELIGIBLE_REL_ABS=0.003
```

The current overlay has runnable OpenFHE examples for interaction logistic regression, higher-order interaction logistic regression, MLP2, dense MLP2, and a multi-layer polynomial network. It does not include full CNN/CryptoNets/Transformer model implementations.

## LogReg And MLP2

Configuration: `N=4096`, `sample_count=64`, 10 seeds.

| Model | det success | indep success | product_safe success | product_safe vs indep Fisher p | det MAE | indep MAE (success only) | product_safe MAE | product_safe Acc |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| LogReg interaction-2way | 10/10 | 3/10 | 10/10 | 0.001548 | 3.554e-06 | 7.264e-01 | 3.415e-06 | 1.000000 |
| LogReg interaction-3way | 10/10 | 3/10 | 10/10 | 0.001548 | 3.147e-06 | 6.396e-01 | 3.327e-06 | 1.000000 |
| LogReg interaction-4way | 10/10 | 3/10 | 10/10 | 0.001548 | 2.922e-06 | 1.370e+00 | 2.947e-06 | 1.000000 |
| MLP2 sparse | 10/10 | 3/10 | 10/10 | 0.001548 | 5.212e-07 | 1.169e+00 | 5.258e-07 | 1.000000 |

## Dense MLP2

Configuration: `N=65536`, `slots=32768`, `sample_count=32768`, 10 seeds.

| Mode | Success | Mean logit MAE | Mean logit MSE | Mean accuracy |
|---|---:|---:|---:|---:|
| deterministic baseline | 10/10 | 2.758343e-05 | 1.308939e-09 | 0.999924 |
| tuned product_safe | 10/10 | 2.752456e-05 | 1.302715e-09 | 0.999911 |

## Multi-Layer Polynomial Network

Configuration: `N=65536`, `slots=16384`, `sample_count=16384`, seed 1.

| Depth | deterministic | independent | product_safe |
|---:|---|---|---|
| 2 | success, acc 0.998840 | fail | success, acc 0.998535 |
| 4 | success, acc 1.000000 | fail | success, acc 1.000000 |
| 6 | success, acc 1.000000 | fail | success, acc 1.000000 |
| 8 | success, acc 1.000000 | fail | success, acc 1.000000 |

## Claim Boundary

These results support model/workload generality for product_safe vs independent randomization. They do not support a claim that product_safe is significantly more accurate than deterministic evaluation.

