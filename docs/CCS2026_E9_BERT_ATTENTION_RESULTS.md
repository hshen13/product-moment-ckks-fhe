# E9 BERT-Style Attention QK^T Results

Input CSV: `/workspace/results/ccs2026_product/e9_bert_attention_productmode_8tok_5seed.csv`

Configuration: tokens=8, dim=768, slots=1024, degree=59, keep_prob=0.60, complete product-coupled mode, correlated Q/K, 5 seeds = 40 attention rows.

| Mode | Success | Mean score MAE | p95 score MAE | Mean softmax MAE | p95 softmax MAE | Top-1 match | Mean latency ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| deterministic | 40/40 | 0.0000e+00 | 0.0000e+00 | 0.0000e+00 | 0.0000e+00 | 1.000 | 4497.39 |
| independent | 40/40 | 1.2077e+01 | 1.7384e+01 | 1.7305e-01 | 1.8201e-01 | 1.000 | 4652.39 |
| product_coupled | 40/40 | 1.0966e+01 | 1.5581e+01 | 1.7239e-01 | 1.8201e-01 | 1.000 | 4613.83 |

## Product-Coupled vs Independent

| Metric | Ratio product/independent | Product wins | p-value |
|---|---:|---:|---:|
| score_mae | 0.9079 | 23/40 | 1.575e-02 |
| score_max_abs | 0.8653 | 16/40 | 1.734e-01 |
| softmax_mae | 0.9962 | 16/40 | 6.963e-02 |

Interpretation: complete product-coupled mode substantially reduces attention-score error. Softmax is less sensitive in this correlated-key setup because the top-1 margin is preserved by both modes, but product-coupled still reduces the score perturbation directly.
