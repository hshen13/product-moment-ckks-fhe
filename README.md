Product-Moment CKKS-FHE
Product-coupled Bernoulli thinning for CKKS bootstrapping in OpenFHE.
This repository contains the source overlays, benchmark drivers, and result summaries used to evaluate a product-moment-aware randomized CKKS bootstrap. The code is designed as an OpenFHE source overlay, not as a standalone OpenFHE checkout: copy either the CPU or GPU overlay into a compatible OpenFHE tree, rebuild examples, and then run the provided experiment scripts.
What this project does
CKKS bootstrapping restores multiplicative depth, but it also introduces approximation error. A single bootstrap error may be small, yet downstream workloads often multiply two bootstrapped values immediately afterward: attention scores, inner products, squared distances, polynomial activations, and compact encrypted ML heads all have this structure.
The core problem studied here is not only pointwise bootstrap error. It is the product structure of bootstrap error.
The method implemented in this repository uses:
Bernoulli coefficient thinning inside the Paterson--Stockmeyer evaluation of the CKKS bootstrap polynomial.
Product-coupled selector scheduling, so the two branches of a downstream product reuse coordinated Bernoulli selectors rather than independent noise.
Tail/head and PS-tree protection, so coefficients that dominate approximation quality or variance amplification remain deterministic.
OpenFHE CPU and GPU overlays for reproducing kernel, bootstrap, and end-to-end encrypted workload experiments.
The high-level idea is that Bernoulli selectors satisfy `A^2 = A`. With the right scaling, this gives a clean second-moment identity for products, which is the algebraic reason product-coupled randomization behaves differently from naive independent randomization.
Repository layout
```text
.
├── cpu_openfhe/                  # CPU OpenFHE source overlay
├── gpu_openfhe/                  # GPU OpenFHE source overlay
├── docs/                         # Experiment reports and runbooks
├── results/ccs2026_heprs/        # Included result summaries / HEPRS artifacts
├── scripts/                      # Experiment runners and summarizers
├── third_party/HEPRS/            # Third-party HEPRS-related material
├── openfhe_ccfa_ccs2026_experiments_ready.zip
└── README.md
```
This package intentionally stores only the modified source files, scripts, and concise documentation needed for review and reproduction. It does not include full OpenFHE source trees, build products, large raw result dumps, or unrelated local files.
Main modified OpenFHE paths
The CPU and GPU overlays modify the CKKS-RNS bootstrap path and add benchmark examples. The most important files are:
```text
src/pke/include/cryptocontext.h
src/pke/include/scheme/ckksrns/ckksrns-fhe.h
src/pke/include/schemebase/base-fhe.h
src/pke/include/schemebase/base-scheme.h
src/pke/lib/scheme/ckksrns/ckksrns-advancedshe.cpp
src/pke/lib/scheme/ckksrns/ckksrns-fhe.cpp
```
Key benchmark examples include:
```text
ccfa-full-bootstrap-probe.cpp
ccfa-full-bootstrap-structure-bench.cpp
ccfa-bias-probe.cpp
ccfa-bound-sweep-bench.cpp
ccfa-coupling-ablation-bench.cpp
ccfa-degree-compare-bench.cpp
ccfa-distribution-ablation-bench.cpp
ccfa-dense-mlp2-bench.cpp
ccfa-he-logreg-bench.cpp
ccfa-he-mlp-bench.cpp
ccfa-he-mlp2-bench.cpp
ccfa-multi-layer-bench.cpp
ccfa-product-kernel.cpp
ccfa-shell-lipschitz-probe.cpp
ccfa-bert-attention-bench.cpp
ccfa-transformer-inner-product-bench.cpp
```
Some `nc-*` examples are non-CCFA diagnostic benchmarks for linear transforms, rotations, FFT-style bootstrap probes, and related OpenFHE runtime checks.
Requirements
You need a working OpenFHE source tree and a standard C++ build environment.
For CPU experiments:
CMake
C++17-capable compiler
OpenFHE CPU source tree compatible with the patched files
Python 3 for summary scripts
For GPU experiments:
A compatible OpenFHE GPU fork
CUDA-capable environment configured for that fork
CMake and C++ build tools
Python 3 for summary scripts
The repository is an overlay. Version mismatches in OpenFHE headers or CKKS internals may require manual patching.
Quick start: CPU overlay
From a clean OpenFHE CPU checkout:
```bash
# From this repository
cp -r cpu_openfhe/* /path/to/openfhe-development/

cd /path/to/openfhe-development
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build -j

export OPENFHE_EXAMPLE_BIN_DIR=/path/to/openfhe-development/build/bin/examples/pke
```
Run a smoke pass first:
```bash
CCS2026_SMOKE=1 \
CCS2026_EXPERIMENTS=E3,E4,E5,E7,E9 \
./scripts/run_ccs2026_experiments.sh
```
Run the full default suite:
```bash
./scripts/run_ccs2026_experiments.sh
```
By default, results are written to:
```text
/workspace/results/ccs2026
```
Override the output directory with:
```bash
export CCS2026_RESULT_DIR=/path/to/results
```
Quick start: GPU overlay
From a clean compatible OpenFHE GPU checkout:
```bash
# From this repository
cp -r gpu_openfhe/* /path/to/openfhe-gpu-public/

cd /path/to/openfhe-gpu-public
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build -j

export OPENFHE_EXAMPLE_BIN_DIR=/path/to/openfhe-gpu-public/build/bin/examples/pke
```
Run a smoke pass:
```bash
CCS2026_SMOKE=1 \
CCS2026_EXPERIMENTS=E3,E4,E5,E6,E7,E9 \
./scripts/run_ccs2026_experiments.sh
```
Then run the selected GPU or dense-packing experiments described in `docs/`.
Experiment drivers
General CCS 2026 suite
```bash
./scripts/run_ccs2026_experiments.sh
```
Default experiments:
ID	Purpose
`E1`	Dense MLP2 packing benchmark
`E2`	Encrypted multi-layer depth scaling
`E3`	Degree-59 vs degree-119 baseline comparison
`E4`	Bernoulli / Gaussian / Uniform distribution ablation
`E5`	Independent / within-level / product-coupled / fully-coupled ablation
`E6`	SOTA bootstrap composition checks
`E7`	End-to-end bootstrap bias grid
`E8`	Failure-rate sweep over keep probabilities
`E9`	Shell Lipschitz probe
Select a subset with:
```bash
CCS2026_EXPERIMENTS=E3,E4,E5 ./scripts/run_ccs2026_experiments.sh
```
Product-sensitive workloads
These experiments focus on workloads where the product structure of bootstrap error matters directly.
```bash
./scripts/run_product_sensitive_experiments.sh
```
Default product-sensitive experiments:
ID	Workload	Main output
`E9`	BERT-style single-head `QK^T` attention	`e9_bert_attention.csv`
`E10`	Transformer-scale encrypted inner product	`e10_inner_product.csv`
Use a short validation pass with:
```bash
CCS2026_SMOKE=1 ./scripts/run_product_sensitive_experiments.sh
```
Layered paper-narrative suite
```bash
./scripts/run_layered_ccs2026_experiments.sh
```
Default layered experiments:
ID	Purpose
`L1`	OpenFHE advanced-CKKS-style randomization speed test
`E9`	BERT-style attention score benchmark
`E10`	Transformer-scale encrypted inner product
To include the aggressive inner-product speed regime:
```bash
CCS2026_LAYERED_EXPERIMENTS=L1,E9,E10,E10_SPEED \
./scripts/run_layered_ccs2026_experiments.sh
```
Important environment variables
The benchmark examples are configured primarily through environment variables.
Variable	Meaning
`OPENFHE_EXAMPLE_BIN_DIR`	Directory containing rebuilt OpenFHE example binaries
`CCS2026_RESULT_DIR`	Output directory for the general CCS 2026 suite
`CCS2026_PRODUCT_RESULT_DIR`	Output directory for product-sensitive experiments
`CCS2026_LAYERED_RESULT_DIR`	Output directory for layered experiments
`CCS2026_SMOKE=1`	Reduce seed counts / trials for a quick compile-runtime validation
`CCS2026_EXPERIMENTS`	Comma-separated subset of general experiments
`CCS2026_PRODUCT_EXPERIMENTS`	Comma-separated subset of product-sensitive experiments
`CCS2026_LAYERED_EXPERIMENTS`	Comma-separated subset of layered experiments
`OPENFHE_CCFA_BOOT_KEEP_PROB`	Bernoulli keep probability `p`
`OPENFHE_CCFA_MIN_SCALE`	Minimum scaling floor `s_min`
`OPENFHE_CCFA_PROTECT_HEAD`	Number of leading Chebyshev coefficients kept deterministic
`OPENFHE_CCFA_PROTECT_TAIL`	Number of trailing / protected coefficients kept deterministic
`OPENFHE_CCFA_ELIGIBLE_REL_ABS`	Relative-magnitude threshold for randomization eligibility
`OPENFHE_CCFA_BOOT_RING_DIM`	CKKS ring dimension for selected experiments
`OPENFHE_CCFA_BOOT_SLOTS`	Number of CKKS slots for selected experiments
`OPENFHE_CCFA_BOOT_LEVELS_AFTER`	Levels after bootstrap
`OPENFHE_CCFA_BOOT_LEVEL_BUDGET0`	First bootstrap level-budget component
`OPENFHE_CCFA_BOOT_LEVEL_BUDGET1`	Second bootstrap level-budget component
Reading the results
Most scripts write CSV files plus a Markdown summary under the selected result directory. Useful summaries include:
```text
docs/OPENFHE_CCFA_CPU_GPU_END2END_REPORT.md
docs/GPU_ONLY_LATEST_RESULTS.md
docs/CCS2026_EXPERIMENT_RUNBOOK.md
docs/CCS2026_LAYERED_EXPERIMENT_RESULTS.md
docs/CCS2026_COMPLETION_RESULTS.md
```
A good reproduction order is:
Build the CPU or GPU OpenFHE tree.
Run a smoke pass.
Run `E3`, `E4`, and `E5` to validate degree, distribution, and coupling behavior.
Run `E9` and `E10` product-sensitive workloads.
Run dense or GPU-specific suites only after the smaller workloads pass.
Key observations from the included reports
The experiments should be interpreted as a layered systems story, not as a universal claim that product-coupled randomization beats deterministic bootstrapping everywhere.
The strongest supported points are:
Product-coupled randomization is qualitatively different from naive independent randomization.
Bernoulli coupling provides structural second-moment control through the selector identity `A^2 = A`.
Product-coupled mode is most useful on product-sensitive workloads such as encrypted inner products, attention-style score products, and interaction-heavy encrypted ML heads.
Dense and GPU configurations require backend-specific tuning; some dense GPU settings show latency gains while fidelity improvements are not always stable.
Independent or noise-flood-style randomization is often unstable in the downstream encrypted tasks used here.
Do not overstate the repository as proving that product-coupled bootstrapping is always faster or always more accurate. The intended claim is narrower: product-coupled randomization defines a usable, analyzable design point for CKKS workloads whose downstream error is dominated by products of bootstrapped values.
Minimal conceptual background
The paper behind this code studies the CKKS bootstrap approximation error
```text
e(z) = g_M(z) - g(z)
```
where `g_M` is the polynomial approximation used during EvalMod. Even if `||e||_∞` is small, `e` is not multiplicatively homomorphic. As a result, products of bootstrapped values contain structured, deterministic error terms that are hard to analyze by pointwise error alone.
Product-coupled Bernoulli thinning changes the analysis target. Instead of only shrinking `||e||_∞`, it introduces controlled randomness inside the polynomial-evaluation coefficients:
```text
a_j -> (A_j / s_j) a_j,     A_j ~ Bernoulli(p_j)
```
When product branches share the relevant selector, Bernoulli idempotence gives
```text
A_j^2 = A_j
```
and therefore a closed-form second-moment expression. The implementation then protects coefficients that are too important or too variance-amplified to randomize safely.
What is protected and why
Randomizing every coefficient is not safe. The overlays implement several protection mechanisms:
Protection	Purpose
Head protection	Keep large leading Chebyshev coefficients deterministic
Tail / threshold protection	Avoid randomizing coefficients where numerical benefit is small or instability risk is high
PS-tree protection	Avoid coefficients whose perturbations are highly amplified along Paterson--Stockmeyer high branches
Minimum scale floor	Prevent excessive coefficient rescaling when keep probability is small
These protections are part of the method, not optional tuning decorations. Without them, aggressive thinning can make bootstrap output fail to decode or can overwhelm downstream task margins.
Reproducibility notes
Always run smoke tests before long sweeps.
Use paired seeds when comparing deterministic, independent, product-coupled, and noise-flood baselines.
Keep CPU and GPU result directories separate.
Store raw CSVs outside this repository unless they are small and intentionally curated.
When reporting performance, specify ring dimension, slot count, level budget, levels after bootstrap, keep probability, scale floor, protected head/tail counts, and seed range.
Known limitations
This repository is an overlay, so it depends on a compatible OpenFHE source tree.
The full CPU/GPU build environment is not vendored here.
Large encrypted ML or third-party workloads may take hours and are not all included as finished full-scale tables.
Some GPU dense-packing runs show latency or small-sample trends but do not yet establish stable fidelity gains.
The IND-CPA argument is preservation relative to the underlying CKKS/OpenFHE encryption model; it does not claim to solve CKKS decryption-oracle leakage issues such as IND-CPA-D.
Troubleshooting
`missing executable: ...`
The overlay was copied, but OpenFHE was not rebuilt with examples, or `OPENFHE_EXAMPLE_BIN_DIR` points to the wrong directory.
```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build -j
export OPENFHE_EXAMPLE_BIN_DIR=/path/to/build/bin/examples/pke
```
GPU slot-precomputation mismatch
Use GPU configurations that explicitly align plaintext and ciphertext slots. The GPU overlay adds sparse-slot handling for the stable GPU benchmark regimes, but older or incompatible GPU trees may still require manual alignment.
Smoke pass works but full pass is very slow
This is expected for large ring dimensions, dense packing, or multi-seed encrypted ML benchmarks. Use `CCS2026_SMOKE=1` and selected `CCS2026_EXPERIMENTS` while debugging.
Product-coupled is not always better than deterministic
That is expected. The method is designed for product-sensitive error structure, not as a universal pointwise-precision improvement. Use the product-sensitive experiments to evaluate the intended regime.
Citation
If you use this repository in a paper, please cite the accompanying paper/preprint once available. Until then, cite the repository as:
```bibtex
@misc{product_moment_ckks_fhe,
  title        = {Product-Moment CKKS-FHE: Product-Coupled Bernoulli Thinning for CKKS Bootstrapping},
  author       = {Shen, H.},
  year         = {2026},
  howpublished = {GitHub repository},
  note         = {OpenFHE CPU/GPU source overlays and CCS 2026 experiment drivers}
}
```
License
Add the intended license before public release. Because this repository contains source overlays for OpenFHE-derived code paths, verify compatibility with the upstream OpenFHE license and any GPU fork license before distribution.
