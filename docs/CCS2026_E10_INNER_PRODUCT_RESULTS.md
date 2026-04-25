# E10 Transformer-Scale Inner Product Results

Input CSV: `/workspace/results/ccs2026_product/e10_inner_product_productmode_p050_100pair.csv`

Configuration: dim=768, slots=1024, degree=59, keep_prob=0.50, complete product-coupled mode, correlated pairs y=x+noise, 100 pairs.

| Mode | Success | Mean rel err | p95 rel err | Mean abs err | p95 abs err | Mean error corr | Mean latency ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| deterministic | 100/100 | 0.0000e+00 | 0.0000e+00 | 0.0000e+00 | 0.0000e+00 | 0.0000e+00 | 322.31 |
| independent | 100/100 | 3.7490e+01 | 9.5165e+01 | 1.6910e+03 | 4.0317e+03 | 5.8123e-01 | 300.73 |
| product_coupled | 100/100 | 1.4262e+01 | 2.0295e+01 | 6.5231e+02 | 8.8204e+02 | 6.1931e-01 | 297.63 |

## Product-Coupled vs Independent

| Metric | Ratio product/independent | Product wins | p-value |
|---|---:|---:|---:|
| rel_error | 0.3804 | 74/100 | 5.204e-10 |
| abs_error | 0.3857 | 74/100 | 5.093e-10 |

Interpretation: product-coupled gives a strong advantage in the lower-keep-probability inner-product regime. This is the intended stress test: independent randomization destroys second-order products, while product-coupled preserves much more of the product structure.
