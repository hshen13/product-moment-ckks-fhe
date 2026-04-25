#!/usr/bin/env bash
set -euo pipefail

BIN_DIR="${OPENFHE_EXAMPLE_BIN_DIR:-${1:-/workspace/openfhe-gpu-public/build/bin/examples/pke}}"
RESULT_DIR="${CCS2026_RESULT_DIR:-/workspace/results/ccs2026}"
SMOKE="${CCS2026_SMOKE:-0}"
EXPERIMENTS="${CCS2026_EXPERIMENTS:-E1,E2,E3,E4,E5,E6,E7,E8,E9}"

mkdir -p "${RESULT_DIR}"

require_bin() {
  local exe="$1"
  if [[ ! -x "${BIN_DIR}/${exe}" ]]; then
    echo "missing executable: ${BIN_DIR}/${exe}" >&2
    echo "copy cpu_openfhe/ or gpu_openfhe/ into a full OpenFHE tree, rebuild, then set OPENFHE_EXAMPLE_BIN_DIR" >&2
    exit 2
  fi
}

has_exp() {
  [[ ",${EXPERIMENTS}," == *",$1,"* ]]
}

seed_end() {
  if [[ "${SMOKE}" == "1" ]]; then
    echo 1
  else
    echo "${1}"
  fi
}

run_e1_dense() {
  require_bin ccfa-dense-mlp2-bench
  local end_seed
  end_seed="$(seed_end 10)"
  local base_env=(
    OPENFHE_CCFA_USE_GPU=1
    OPENFHE_CCFA_BOOT_RING_DIM=65536
    OPENFHE_CCFA_BOOT_SLOTS=32768
    OPENFHE_CCFA_SAMPLE_COUNT=32768
    OPENFHE_CCFA_BOOT_LEVELS_AFTER=20
    OPENFHE_CCFA_BOOT_LEVEL_BUDGET0=4
    OPENFHE_CCFA_BOOT_LEVEL_BUDGET1=4
    OPENFHE_CCFA_BOOT_KEEP_PROB=0.91
    OPENFHE_CCFA_MIN_SCALE=0.992
    OPENFHE_CCFA_PROTECT_HEAD="${OPENFHE_CCFA_TUNED_PROTECT:-8}"
    OPENFHE_CCFA_PROTECT_TAIL="${OPENFHE_CCFA_TUNED_PROTECT:-8}"
    OPENFHE_CCFA_ELIGIBLE_REL_ABS="${OPENFHE_CCFA_TUNED_ELIGIBLE_REL_ABS:-0.003}"
    OPENFHE_CCFA_DATA_SEED_START=1
    OPENFHE_CCFA_DATA_SEED_END="${end_seed}"
  )
  echo "[E1] dense deterministic"
  env "${base_env[@]}" OPENFHE_CCFA_HE_MLP2_SINGLE=deterministic \
    OPENFHE_CCFA_HE_MLP2_OUTPUT="${RESULT_DIR}/e1_dense_det.csv" "${BIN_DIR}/ccfa-dense-mlp2-bench"
  echo "[E1] dense product_safe"
  env "${base_env[@]}" OPENFHE_CCFA_HE_MLP2_SINGLE=product_safe \
    OPENFHE_CCFA_HE_MLP2_OUTPUT="${RESULT_DIR}/e1_dense_product_safe.csv" "${BIN_DIR}/ccfa-dense-mlp2-bench"
  echo "[E1] dense noise_flood"
  env "${base_env[@]}" OPENFHE_CCFA_HE_MLP2_SINGLE=deterministic OPENFHE_CCFA_NOISE_FLOOD_ENABLE=1 \
    OPENFHE_CCFA_NOISE_FLOOD_SIGMA=1e-6 \
    OPENFHE_CCFA_HE_MLP2_OUTPUT="${RESULT_DIR}/e1_dense_noise_flood.csv" "${BIN_DIR}/ccfa-dense-mlp2-bench"
}

run_e2_depth() {
  require_bin ccfa-multi-layer-bench
  local end_seed
  end_seed="$(seed_end 10)"
  echo "[E2] encrypted multi-layer depth scaling"
  env OPENFHE_CCFA_BOOT_RING_DIM=65536 OPENFHE_CCFA_BOOT_SLOTS=16384 \
    OPENFHE_CCFA_BOOT_LEVELS_AFTER=20 OPENFHE_CCFA_BOOT_LEVEL_BUDGET0=4 OPENFHE_CCFA_BOOT_LEVEL_BUDGET1=4 \
    OPENFHE_CCFA_BOOT_KEEP_PROB=0.91 OPENFHE_CCFA_MIN_SCALE=0.992 \
    OPENFHE_CCFA_PROTECT_HEAD="${OPENFHE_CCFA_TUNED_PROTECT:-8}" \
    OPENFHE_CCFA_PROTECT_TAIL="${OPENFHE_CCFA_TUNED_PROTECT:-8}" \
    OPENFHE_CCFA_ELIGIBLE_REL_ABS="${OPENFHE_CCFA_TUNED_ELIGIBLE_REL_ABS:-0.003}" \
    OPENFHE_CCFA_DATA_SEED_START=1 OPENFHE_CCFA_DATA_SEED_END="${end_seed}" \
    OPENFHE_CCFA_SAMPLE_COUNT=256 OPENFHE_CCFA_MAX_LAYERS=8 \
    OPENFHE_CCFA_MULTI_OUTPUT="${RESULT_DIR}/e2_multi_layer.csv" "${BIN_DIR}/ccfa-multi-layer-bench"
}

run_e3_degree() {
  require_bin ccfa-degree-compare-bench
  local seeds
  seeds="1,2,3,4,5,6,7,8,9,10"
  [[ "${SMOKE}" == "1" ]] && seeds="1"
  echo "[E3] degree 59"
  env OPENFHE_CCFA_SYNTH_DEGREE=59 OPENFHE_CCFA_KEEP_LIST=0.6 OPENFHE_CCFA_SEED_LIST="${seeds}" \
    OPENFHE_CCFA_OUTPUT="${RESULT_DIR}/e3_degree59.csv" "${BIN_DIR}/ccfa-degree-compare-bench"
  echo "[E3] degree 119"
  env OPENFHE_CCFA_SYNTH_DEGREE=119 OPENFHE_CCFA_KEEP_LIST=0.6 OPENFHE_CCFA_SEED_LIST="${seeds}" \
    OPENFHE_CCFA_OUTPUT="${RESULT_DIR}/e3_degree119.csv" "${BIN_DIR}/ccfa-degree-compare-bench"
}

run_e4_distribution() {
  require_bin ccfa-distribution-ablation-bench
  local seeds
  seeds="1,2,3,4,5,6,7,8,9,10"
  [[ "${SMOKE}" == "1" ]] && seeds="1"
  for dist in bernoulli gaussian uniform; do
    echo "[E4] distribution ${dist}"
    env OPENFHE_CCFA_DIST="${dist}" OPENFHE_CCFA_COUPLING=product_coupled \
      OPENFHE_CCFA_KEEP_LIST=0.6 OPENFHE_CCFA_SEED_LIST="${seeds}" \
      OPENFHE_CCFA_OUTPUT="${RESULT_DIR}/e4_distribution_${dist}.csv" \
      "${BIN_DIR}/ccfa-distribution-ablation-bench"
  done
}

run_e5_coupling() {
  require_bin ccfa-coupling-ablation-bench
  local seeds
  seeds="1,2,3,4,5,6,7,8,9,10"
  [[ "${SMOKE}" == "1" ]] && seeds="1"
  for coupling in independent within_level product_coupled fully; do
    echo "[E5] coupling ${coupling}"
    env OPENFHE_CCFA_DIST=bernoulli OPENFHE_CCFA_COUPLING="${coupling}" \
      OPENFHE_CCFA_KEEP_LIST=0.6 OPENFHE_CCFA_SEED_LIST="${seeds}" \
      OPENFHE_CCFA_OUTPUT="${RESULT_DIR}/e5_coupling_${coupling}.csv" \
      "${BIN_DIR}/ccfa-coupling-ablation-bench"
  done
}

run_e6_sota_bootstrap() {
  require_bin ccfa-sota-bootstrap-bench
  local seeds
  seeds="$(seed_end 10)"
  echo "[E6] SOTA bootstrap composition"
  env OPENFHE_CCFA_E6_SEEDS="${seeds}" OPENFHE_CCFA_E6_FIRST_SEED=1 \
    OPENFHE_CCFA_E6_RING_DIM="${OPENFHE_CCFA_E6_RING_DIM:-4096}" \
    OPENFHE_CCFA_E6_INPUT_N="${OPENFHE_CCFA_E6_INPUT_N:-2048}" \
    OPENFHE_CCFA_TUNED_KEEP_PROB="${OPENFHE_CCFA_TUNED_KEEP_PROB:-0.91}" \
    OPENFHE_CCFA_TUNED_MIN_SCALE="${OPENFHE_CCFA_TUNED_MIN_SCALE:-0.992}" \
    OPENFHE_CCFA_TUNED_PROTECT="${OPENFHE_CCFA_TUNED_PROTECT:-8}" \
    OPENFHE_CCFA_TUNED_ELIGIBLE_REL_ABS="${OPENFHE_CCFA_TUNED_ELIGIBLE_REL_ABS:-0.003}" \
    OPENFHE_CCFA_E6_OUTPUT="${RESULT_DIR}/e6_sota_bootstrap.csv" \
    "${BIN_DIR}/ccfa-sota-bootstrap-bench"
}

run_e7_bias() {
  require_bin ccfa-bias-probe
  echo "[E7] bias grid"
  env OPENFHE_CCFA_BOOT_RING_DIM=65536 OPENFHE_CCFA_BOOT_SLOTS=32768 OPENFHE_CCFA_BOOT_LEVELS_AFTER=20 \
    OPENFHE_CCFA_PROTECT_HEAD="${OPENFHE_CCFA_TUNED_PROTECT:-8}" \
    OPENFHE_CCFA_PROTECT_TAIL="${OPENFHE_CCFA_TUNED_PROTECT:-8}" \
    OPENFHE_CCFA_ELIGIBLE_REL_ABS="${OPENFHE_CCFA_TUNED_ELIGIBLE_REL_ABS:-0.003}" \
    OPENFHE_CCFA_BIAS_GRID_N=32768 OPENFHE_CCFA_BIAS_OUTPUT="${RESULT_DIR}/e7_bias.csv" \
    "${BIN_DIR}/ccfa-bias-probe"
}

run_e8_bound() {
  require_bin ccfa-bound-sweep-bench
  local end_seed
  end_seed="$(seed_end 50)"
  for p in 0.10 0.20 0.30 0.40 0.50 0.60; do
    echo "[E8] bound sweep p=${p}"
    env OPENFHE_CCFA_PROTECT_HEAD="${OPENFHE_CCFA_TUNED_PROTECT:-8}" \
      OPENFHE_CCFA_PROTECT_TAIL="${OPENFHE_CCFA_TUNED_PROTECT:-8}" \
      OPENFHE_CCFA_ELIGIBLE_REL_ABS="${OPENFHE_CCFA_TUNED_ELIGIBLE_REL_ABS:-0.003}" \
      OPENFHE_CCFA_BOOT_KEEP_PROB="${p}" OPENFHE_CCFA_DATA_SEED_START=1 OPENFHE_CCFA_DATA_SEED_END="${end_seed}" \
      OPENFHE_CCFA_HE_LOGREG_OUTPUT="${RESULT_DIR}/e8_bound_p${p}.csv" "${BIN_DIR}/ccfa-bound-sweep-bench"
  done
}

run_e9_lipschitz() {
  require_bin ccfa-shell-lipschitz-probe
  local trials=200
  [[ "${SMOKE}" == "1" ]] && trials=2
  echo "[E9] shell Lipschitz"
  env OPENFHE_NC_LIPSCHITZ_TRIALS="${trials}" \
    OPENFHE_NC_LIPSCHITZ_OUTPUT="${RESULT_DIR}/e9_lipschitz.csv" "${BIN_DIR}/ccfa-shell-lipschitz-probe"
}

has_exp E1 && run_e1_dense
has_exp E2 && run_e2_depth
has_exp E3 && run_e3_degree
has_exp E4 && run_e4_distribution
has_exp E5 && run_e5_coupling
has_exp E6 && run_e6_sota_bootstrap
has_exp E7 && run_e7_bias
has_exp E8 && run_e8_bound
has_exp E9 && run_e9_lipschitz

python3 "$(dirname "$0")/summarize_ccs2026_results.py" "${RESULT_DIR}" > "${RESULT_DIR}/ccs2026_experiment_summary.md"
if [[ -f "${RESULT_DIR}/e6_sota_bootstrap.csv" ]]; then
  python3 "$(dirname "$0")/summarize_e6_sota.py" "${RESULT_DIR}/e6_sota_bootstrap.csv" \
    "${RESULT_DIR}/e6_sota_bootstrap_summary.md" >/dev/null
fi
echo "summary: ${RESULT_DIR}/ccs2026_experiment_summary.md"
