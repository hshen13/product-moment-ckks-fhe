#!/usr/bin/env bash
set -euo pipefail

BIN_DIR="${OPENFHE_EXAMPLE_BIN_DIR:-${1:-/workspace/openfhe-development-v142-min/build-ccfa/bin/examples/pke}}"
RESULT_DIR="${CCS2026_LAYERED_RESULT_DIR:-/workspace/results/ccs2026_layered}"
SMOKE="${CCS2026_SMOKE:-0}"
EXPERIMENTS="${CCS2026_LAYERED_EXPERIMENTS:-L1,E9,E10}"

mkdir -p "${RESULT_DIR}"

require_bin() {
  local exe="$1"
  if [[ ! -x "${BIN_DIR}/${exe}" ]]; then
    echo "missing executable: ${BIN_DIR}/${exe}" >&2
    exit 2
  fi
}

has_exp() {
  [[ ",${EXPERIMENTS}," == *",$1,"* ]]
}

run_l1_randomization_speed() {
  require_bin ccfa-randomization-speed-bench
  local seeds=5
  local reps=3
  if [[ "${SMOKE}" == "1" ]]; then
    seeds=1
    reps=1
  fi
  echo "[L1] OpenFHE advanced-ckks-style randomization speed"
  env OPENFHE_CCFA_L1_SEEDS="${seeds}" \
    OPENFHE_CCFA_L1_REPS="${reps}" \
    OPENFHE_CCFA_L1_KEEP_PROB="${OPENFHE_CCFA_L1_KEEP_PROB:-0.50}" \
    OPENFHE_CCFA_L1_PROTECT="${OPENFHE_CCFA_L1_PROTECT:-16}" \
    OPENFHE_CCFA_L1_OUTPUT="${RESULT_DIR}/l1_randomization_speed.csv" \
    OPENFHE_CCFA_L1_PROFILE_DIR="${RESULT_DIR}/l1_profiles" \
    "${BIN_DIR}/ccfa-randomization-speed-bench"
}

run_e9_attention() {
  require_bin ccfa-bert-attention-bench
  local seeds=5
  local tokens=8
  if [[ "${SMOKE}" == "1" ]]; then
    seeds=1
    tokens=4
  fi
  echo "[E9/L3.2] BERT-style QK^T attention"
  env OPENFHE_CCFA_E9_SEEDS="${seeds}" \
    OPENFHE_CCFA_E9_TOKENS="${tokens}" \
    OPENFHE_CCFA_E9_DIM=768 \
    OPENFHE_CCFA_E9_KEEP_PROB="${OPENFHE_CCFA_E9_KEEP_PROB:-0.60}" \
    OPENFHE_CCFA_E9_OUTPUT="${RESULT_DIR}/l3_bert_attention.csv" \
    "${BIN_DIR}/ccfa-bert-attention-bench"
}

run_e10_inner_product() {
  require_bin ccfa-transformer-inner-product-bench
  local pairs=100
  if [[ "${SMOKE}" == "1" ]]; then
    pairs=5
  fi
  echo "[E10/L3.1] transformer-scale encrypted inner product"
  env OPENFHE_CCFA_E10_PAIRS="${pairs}" \
    OPENFHE_CCFA_E10_DIM=768 \
    OPENFHE_CCFA_E10_KEEP_PROB="${OPENFHE_CCFA_E10_KEEP_PROB:-0.50}" \
    OPENFHE_CCFA_E10_OUTPUT="${RESULT_DIR}/l3_inner_product.csv" \
    "${BIN_DIR}/ccfa-transformer-inner-product-bench"
}

run_e10_speed_regime() {
  require_bin ccfa-transformer-inner-product-bench
  local pairs=50
  if [[ "${SMOKE}" == "1" ]]; then
    pairs=5
  fi
  echo "[L1/L3] transformer inner-product speed regime at p=0.10"
  env OPENFHE_CCFA_E10_PAIRS="${pairs}" \
    OPENFHE_CCFA_E10_DIM=768 \
    OPENFHE_CCFA_E10_KEEP_PROB="${OPENFHE_CCFA_E10_SPEED_KEEP_PROB:-0.10}" \
    OPENFHE_CCFA_E10_OUTPUT="${RESULT_DIR}/l3_inner_product_p010_speed.csv" \
    "${BIN_DIR}/ccfa-transformer-inner-product-bench"
}

has_exp L1 && run_l1_randomization_speed
has_exp E9 && run_e9_attention
has_exp E10 && run_e10_inner_product
has_exp E10_SPEED && run_e10_speed_regime

python3 "$(dirname "$0")/summarize_ccs2026_results.py" "${RESULT_DIR}" > "${RESULT_DIR}/layered_numeric_summary.md"
echo "summary: ${RESULT_DIR}/layered_numeric_summary.md"
