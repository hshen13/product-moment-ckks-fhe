OpenFHE-CCFA: CPU/GPU Modified Sources
This archive contains only the modified source files and English reports for the OpenFHE-CCFA project.
Contents
`cpu_openfhe/`
Modified source files from the CPU OpenFHE tree used for the CCFA experiments.
`gpu_openfhe/`
Modified source files from the GPU OpenFHE fork used for the GPU-only experiments.
`docs/`
English Markdown reports summarizing CPU/GPU and GPU-only experimental results.
Included CPU Files
The CPU package contains the modified files under:
`src/pke/include/cryptocontext.h`
`src/pke/include/scheme/ckksrns/ckksrns-fhe.h`
`src/pke/include/schemebase/base-fhe.h`
`src/pke/include/schemebase/base-scheme.h`
`src/pke/lib/scheme/ckksrns/ckksrns-advancedshe.cpp`
`src/pke/lib/scheme/ckksrns/ckksrns-fhe.cpp`
`src/pke/examples/ccfa-full-bootstrap-probe.cpp`
`src/pke/examples/ccfa-full-bootstrap-structure-bench.cpp`
`src/pke/examples/ccfa-bias-probe.cpp`
`src/pke/examples/ccfa-bound-sweep-bench.cpp`
`src/pke/examples/ccfa-coupling-ablation-bench.cpp`
`src/pke/examples/ccfa-degree-compare-bench.cpp`
`src/pke/examples/ccfa-dense-mlp2-bench.cpp`
`src/pke/examples/ccfa-distribution-ablation-bench.cpp`
`src/pke/examples/ccfa-he-logreg-bench.cpp`
`src/pke/examples/ccfa-he-mlp-bench.cpp`
`src/pke/examples/ccfa-he-mlp2-bench.cpp`
`src/pke/examples/ccfa-multi-layer-bench.cpp`
`src/pke/examples/ccfa-product-kernel.cpp`
`src/pke/examples/ccfa-shell-lipschitz-probe.cpp`
`src/pke/examples/nc-bootstrap-fft-bench.cpp`
`src/pke/examples/nc-bootstrap-insitu-probe.cpp`
`src/pke/examples/nc-lineartransform-bench.cpp`
`src/pke/examples/nc-rotation-bench.cpp`
Included GPU Files
The GPU package contains the modified files under:
`CMakeLists.txt`
`src/pke/CMakeLists.txt`
`src/pke/include/cryptocontext.h`
`src/pke/include/scheme/ckksrns/ckksrns-fhe.h`
`src/pke/include/schemebase/base-fhe.h`
`src/pke/include/schemebase/base-scheme.h`
`src/pke/lib/scheme/ckksrns/ckksrns-advancedshe.cpp`
`src/pke/lib/scheme/ckksrns/ckksrns-fhe.cpp`
`src/pke/examples/ccfa-full-bootstrap-probe.cpp`
`src/pke/examples/ccfa-full-bootstrap-structure-bench.cpp`
`src/pke/examples/ccfa-bias-probe.cpp`
`src/pke/examples/ccfa-bound-sweep-bench.cpp`
`src/pke/examples/ccfa-coupling-ablation-bench.cpp`
`src/pke/examples/ccfa-degree-compare-bench.cpp`
`src/pke/examples/ccfa-dense-mlp2-bench.cpp`
`src/pke/examples/ccfa-distribution-ablation-bench.cpp`
`src/pke/examples/ccfa-he-logreg-bench.cpp`
`src/pke/examples/ccfa-he-mlp-bench.cpp`
`src/pke/examples/ccfa-he-mlp2-bench.cpp`
`src/pke/examples/ccfa-multi-layer-bench.cpp`
`src/pke/examples/ccfa-product-kernel.cpp`
`src/pke/examples/ccfa-shell-lipschitz-probe.cpp`
`src/pke/examples/nc-bootstrap-fft-bench.cpp`
`src/pke/examples/nc-bootstrap-insitu-probe.cpp`
`src/pke/examples/nc-lineartransform-bench.cpp`
`src/pke/examples/nc-rotation-bench.cpp`
How to Use
CPU Tree
Start from your CPU OpenFHE source tree.
Copy the contents of `cpu_openfhe/` into the root of that tree.
Reconfigure and rebuild OpenFHE.
Example:
```bash
cp -r openfhe_ccfa_githubready/cpu_openfhe/* /path/to/openfhe-development/
cd /path/to/openfhe-development
cmake -S . -B build
cmake --build build -j
```
GPU Tree
Start from your GPU OpenFHE fork source tree.
Copy the contents of `gpu_openfhe/` into the root of that tree.
Reconfigure and rebuild the GPU fork.
Example:
```bash
cp -r openfhe_ccfa_githubready/gpu_openfhe/* /path/to/openfhe-gpu-public/
cd /path/to/openfhe-gpu-public
cmake -S . -B build
cmake --build build -j
```
Reports
`docs/OPENFHE_CCFA_CPU_GPU_END2END_REPORT.md`
Combined CPU/GPU summary report.
`docs/GPU_ONLY_LATEST_RESULTS.md`
Latest GPU-only experimental summary.
`docs/CCS2026_EXPERIMENT_RUNBOOK.md`
CCS 2026 experiment runbook for the dense, depth, degree, distribution, coupling, bias, bound, and Lipschitz experiments.
CCS 2026 Experiment Driver
After copying either `cpu_openfhe/` or `gpu_openfhe/` into a full OpenFHE source tree and rebuilding, run:
```bash
export OPENFHE_EXAMPLE_BIN_DIR=/path/to/openfhe/build/bin/examples/pke
./openfhe_ccfa_githubready/scripts/run_ccs2026_experiments.sh
```
For a one-seed compile/runtime smoke pass:
```bash
CCS2026_SMOKE=1 CCS2026_EXPERIMENTS=E3,E4,E5,E7,E9 \
  ./openfhe_ccfa_githubready/scripts/run_ccs2026_experiments.sh
```
The driver writes CSVs and `ccs2026_experiment_summary.md` under `/workspace/results/ccs2026` by default.
Notes
This package intentionally excludes raw result CSVs, binaries, and unrelated repository files.
The archive is designed to be GitHub-ready: code only, plus concise English documentation.
