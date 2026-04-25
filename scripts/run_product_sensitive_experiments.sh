#!/usr/bin/env bash
set -euo pipefail

BIN_DIR="${OPENFHE_EXAMPLE_BIN_DIR:-${1:-/workspace/openfhe-development-v142-min/build-ccfa/bin/examples/pke}}"
RESULT_DIR="${CCS2026_PRODUCT_RESULT_DIR:-/workspace/results/ccs2026_product}"
SMOKE="${CCS2026_SMOKE:-0}"
EXPERIMENTS="${CCS2026_PRODUCT_EXPERIMENTS:-E9,E10}"

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

run_e9_attention() {
  require_bin ccfa-bert-attention-bench
  local seeds=5
  local tokens=8
  if [[ "${SMOKE}" == "1" ]]; then
    seeds=1
    tokens=4
  fi
  echo "[E9] BERT-style QK^T attention"
  env OPENFHE_CCFA_E9_SEEDS="${seeds}" \
    OPENFHE_CCFA_E9_TOKENS="${tokens}" \
    OPENFHE_CCFA_E9_DIM=768 \
    OPENFHE_CCFA_E9_KEEP_PROB="${OPENFHE_CCFA_E9_KEEP_PROB:-0.60}" \
    OPENFHE_CCFA_E9_OUTPUT="${RESULT_DIR}/e9_bert_attention.csv" \
    "${BIN_DIR}/ccfa-bert-attention-bench"
}

run_e10_inner_product() {
  require_bin ccfa-transformer-inner-product-bench
  local pairs=100
  if [[ "${SMOKE}" == "1" ]]; then
    pairs=5
  fi
  echo "[E10] transformer-scale encrypted inner product"
  env OPENFHE_CCFA_E10_PAIRS="${pairs}" \
    OPENFHE_CCFA_E10_DIM=768 \
    OPENFHE_CCFA_E10_KEEP_PROB="${OPENFHE_CCFA_E10_KEEP_PROB:-0.50}" \
    OPENFHE_CCFA_E10_OUTPUT="${RESULT_DIR}/e10_inner_product.csv" \
    "${BIN_DIR}/ccfa-transformer-inner-product-bench"
}

has_exp E9 && run_e9_attention
has_exp E10 && run_e10_inner_product

python3 "$(dirname "$0")/summarize_ccs2026_results.py" "${RESULT_DIR}" > "${RESULT_DIR}/product_sensitive_summary.md"
echo "summary: ${RESULT_DIR}/product_sensitive_summary.md"
