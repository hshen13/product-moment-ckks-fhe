# Randomization Advantage: Budgeted Inner Product

CSV: `/workspace/results/ccs2026_product/e10_inner_product_productmode_p050_100pair.csv`

Configuration: dim=768, degree=59, keep_prob=0.50, 100 correlated pairs. Full deterministic is the accuracy anchor. Randomized rows are budgeted tail-gated polynomial evaluation.

| Mode | Mean latency ms | Latency ratio vs det | Faster than det | Mean rel err | p95 rel err | Mean abs err | p95 abs err |
|---|---:|---:|---:|---:|---:|---:|---:|
| deterministic | 322.31 | 1.0000 | 0/- | 0.0000e+00 | 0.0000e+00 | 0.0000e+00 | 0.0000e+00 |
| independent | 300.73 | 0.9330 | 95/100 | 3.7490e+01 | 9.5165e+01 | 1.6910e+03 | 4.0317e+03 |
| product_coupled | 297.63 | 0.9235 | 93/100 | 1.4262e+01 | 2.0295e+01 | 6.5231e+02 | 8.8204e+02 |

## Paired Tests

| Comparison | Metric | Ratio | Wins | p-value |
|---|---:|---:|---:|---:|
| independent vs deterministic | latency | 0.9330 | 95/100 | <1e-12 |
| product_coupled vs deterministic | latency | 0.9235 | 93/100 | <1e-12 |
| product_coupled vs independent | rel_error | 0.3804 | 74/100 | 8.894e-12 |
| product_coupled vs independent | abs_error | 0.3857 | 74/100 | 2.479e-12 |

Interpretation: randomization gives a measurable compute-speed benefit because zeroed tail coefficients are skipped in scalar-multiply/add accumulation. Product-coupling keeps this speed benefit while greatly reducing the product-sensitive error of naive independent randomization.
