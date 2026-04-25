#!/usr/bin/env bash
set -euo pipefail

ROOT="${ROOT:-/workspace/LowMemoryFHEResNet20}"
BUILD="${BUILD:-$ROOT/build}"
MANIFEST="${MANIFEST:-$ROOT/inputs/cifar10_subset/manifest.csv}"
OUT_DIR="${OUT_DIR:-/workspace/results/ccs2026_mustfix/resnet10_logs}"
CSV_OUT="${CSV_OUT:-/workspace/results/ccs2026_mustfix/resnet_cifar10_10image_3variant.csv}"

mkdir -p "$OUT_DIR"

run_one() {
  local variant="$1"
  local img_path="$2"
  local test_index="$3"
  local stem="$4"
  local log="$OUT_DIR/${stem}_${variant}.log"
  local time_file="$OUT_DIR/${stem}_${variant}.time"
  local seed=$((test_index + 1))
  local start
  local end
  local status=0

  start="$(date +%s)"

  case "$variant" in
    deterministic)
      env OPENFHE_CCFA_MODE=none \
        ./LowMemoryFHEResNet20 load_keys 1 input "$img_path" verbose 0 \
        >"$log" 2>&1 || status=$?
      ;;
    product_safe)
      env OPENFHE_CCFA_MODE=product_safe \
            OPENFHE_CCFA_DIST=bernoulli \
            OPENFHE_CCFA_COUPLING=product_coupled \
            OPENFHE_CCFA_KEEP_PROB=0.91 \
            OPENFHE_CCFA_MIN_SCALE=0.992 \
            OPENFHE_CCFA_SEED="$seed" \
            OPENFHE_CCFA_MIN_M=2 \
            OPENFHE_CCFA_MAX_M=16 \
            OPENFHE_CCFA_SAFE_BOOTSTRAP=1 \
            OPENFHE_CCFA_SAFE_MAX_M=2 \
            OPENFHE_CCFA_SAFE_DISABLE_CU=1 \
            OPENFHE_CCFA_PROTECT_HEAD=8 \
            OPENFHE_CCFA_PROTECT_TAIL=8 \
            OPENFHE_CCFA_ELIGIBLE_REL_ABS=0.003 \
        ./LowMemoryFHEResNet20 load_keys 1 input "$img_path" verbose 0 \
        >"$log" 2>&1 || status=$?
      ;;
    independent)
      env OPENFHE_CCFA_MODE=independent \
            OPENFHE_CCFA_DIST=bernoulli \
            OPENFHE_CCFA_COUPLING=independent \
            OPENFHE_CCFA_KEEP_PROB=0.91 \
            OPENFHE_CCFA_MIN_SCALE=0.992 \
            OPENFHE_CCFA_SEED="$seed" \
            OPENFHE_CCFA_MIN_M=2 \
            OPENFHE_CCFA_MAX_M=16 \
            OPENFHE_CCFA_SAFE_BOOTSTRAP=1 \
            OPENFHE_CCFA_SAFE_MAX_M=2 \
            OPENFHE_CCFA_SAFE_DISABLE_CU=1 \
            OPENFHE_CCFA_PROTECT_HEAD=8 \
            OPENFHE_CCFA_PROTECT_TAIL=8 \
            OPENFHE_CCFA_ELIGIBLE_REL_ABS=0.003 \
        ./LowMemoryFHEResNet20 load_keys 1 input "$img_path" verbose 0 \
        >"$log" 2>&1 || status=$?
      ;;
    *)
      echo "unknown variant: $variant" >&2
      exit 2
      ;;
  esac
  end="$(date +%s)"
  {
    echo "WALL_SECONDS=$((end - start))"
    echo "EXIT_STATUS=$status"
  } >"$time_file"
  return 0
}

cd "$BUILD"

tail -n +2 "$MANIFEST" | while IFS=, read -r img_path label class_name test_index; do
  img_path="${img_path//$'\r'/}"
  label="${label//$'\r'/}"
  class_name="${class_name//$'\r'/}"
  test_index="${test_index//$'\r'/}"
  stem="$(basename "$img_path")"
  stem="${stem%.*}"
  echo "[$(date -u +'%Y-%m-%dT%H:%M:%SZ')] running $stem label=$label class=$class_name"
  run_one deterministic "$img_path" "$test_index" "$stem"
  run_one product_safe "$img_path" "$test_index" "$stem"
  run_one independent "$img_path" "$test_index" "$stem"
done

python3 /workspace/openfhe_ccfa_githubready/scripts/summarize_resnet10_cifar_subset.py \
  --manifest "$MANIFEST" \
  --log-dir "$OUT_DIR" \
  --output "$CSV_OUT"

echo "wrote $CSV_OUT"
