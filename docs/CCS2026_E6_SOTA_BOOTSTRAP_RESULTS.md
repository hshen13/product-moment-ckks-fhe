# E6 SOTA Bootstrap Composition Results

Input: OpenFHE CKKS, ring_dim=4096, slots=2048, input_n=2048, seeds 1..10. Product-coupled uses tuned safe parameters p=0.91, min_scale=0.992, protect=8, eligible_rel_abs=0.003.

## Aggregate

| Family | Randomization | Success | Mean latency ms | Mean max abs err | Mean MAE | Mean precision bits |
|---|---:|---:|---:|---:|---:|---:|
| chebyshev_default | none | 10/10 | 1410.85 | 1.961e-05 | 3.068e-06 | 15.64 |
| chebyshev_default | product_coupled | 10/10 | 1394.88 | 1.663e-05 | 3.052e-06 | 15.89 |
| composite_metabts2 | none | 10/10 | 11566.43 | 2.129e-11 | 4.244e-12 | 35.46 |
| composite_metabts2 | product_coupled | 10/10 | 11557.03 | 2.004e-11 | 4.196e-12 | 35.54 |
| composite_scaling | none | 10/10 | 2855.86 | 3.374e-08 | 6.492e-09 | 24.83 |
| composite_scaling | product_coupled | 10/10 | 2862.83 | 3.314e-08 | 6.549e-09 | 24.86 |
| metabts2 | none | 10/10 | 2721.09 | 1.269e-10 | 2.224e-11 | 32.89 |
| metabts2 | product_coupled | 10/10 | 2710.44 | 1.253e-10 | 2.264e-11 | 32.90 |

## Paired Product-Coupled Effect

Ratios are product_coupled / deterministic. For error and latency, lower is better; for precision bits, higher is better. p-values are exact paired Wilcoxon one-sided tests over seeds 1..10.

| Family | Metric | Ratio | Product wins | p-value | Significant at 0.05? |
|---|---:|---:|---:|---:|---:|
| chebyshev_default | max_abs_error | 0.8479 | 9/10 | 0.0137 | yes |
| chebyshev_default | mae | 0.9950 | 6/10 | 0.1875 | no |
| chebyshev_default | latency_ms | 0.9887 | 6/10 | 0.0654 | no |
| chebyshev_default | precision_bits | 1.0155 | 9/10 | 0.0098 | yes |
| composite_metabts2 | max_abs_error | 0.9410 | 5/10 | 0.1611 | no |
| composite_metabts2 | mae | 0.9886 | 5/10 | 0.3125 | no |
| composite_metabts2 | latency_ms | 0.9992 | 4/10 | 0.5391 | no |
| composite_metabts2 | precision_bits | 1.0022 | 5/10 | 0.1611 | no |
| composite_scaling | max_abs_error | 0.9824 | 5/10 | 0.3477 | no |
| composite_scaling | mae | 1.0088 | 3/10 | 0.9033 | no |
| composite_scaling | latency_ms | 1.0024 | 3/10 | 0.7539 | no |
| composite_scaling | precision_bits | 1.0009 | 5/10 | 0.3477 | no |
| metabts2 | max_abs_error | 0.9877 | 5/10 | 0.3848 | no |
| metabts2 | mae | 1.0181 | 3/10 | 0.9902 | no |
| metabts2 | latency_ms | 0.9961 | 4/10 | 0.4609 | no |
| metabts2 | precision_bits | 1.0002 | 5/10 | 0.3848 | no |

## Interpretation

Product-coupled has statistically significant paired improvement for: chebyshev_default/max_abs_error, chebyshev_default/precision_bits.

Composite and Meta-BTS baselines are much more precise than default Chebyshev in absolute error, as expected. Product-coupled remains stable across all families with 10/10 success in each row and small overhead or no overhead depending on the family.

PaCo was auto-downloaded at `/workspace/PaCo-Implementation`, including submodules, but it is a SageMath proof-of-concept and the current machine has no `sage` executable. It is therefore not included in this same-machine OpenFHE significance table.
