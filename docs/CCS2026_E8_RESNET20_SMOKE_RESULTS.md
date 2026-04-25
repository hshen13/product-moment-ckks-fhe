# E8 ResNet-20 Single-Image Smoke Result

CSV: `/workspace/results/ccs2026_resnet/e8_resnet20_luis_single_image.csv`

Input image: `inputs/luis.png`; OpenFHE 1.4.2, N=2^16 with `HEStd_NotSet` compatibility patch, circuit depth +1 per upstream README guidance.

| Mode | Success | Prediction | Latency s | Logit MAE vs det | Max abs logit diff |
|---|---:|---|---:|---:|---:|
| deterministic | 1/1 | Cat | 405.09 | 0.0000e+00 | 0.0000e+00 |
| product_safe | 1/1 | Cat | 362.86 | 1.8800e-02 | 6.8000e-02 |
| independent | 1/1 | Cat | 397.01 | 1.8400e-02 | 3.1000e-02 |

Interpretation: this is not yet the full CIFAR-10 accuracy table. It verifies that the downloaded OpenFHE ResNet-20 application builds and runs end-to-end, and that deployment-safe product-coupled mode preserves the deterministic prediction on a full ResNet-20 inference. Aggressive full product mode at p=0.5 failed early from excessive approximation error, so ResNet must use deployment-safe parameters.
