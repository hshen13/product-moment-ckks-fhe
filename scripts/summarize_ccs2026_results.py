#!/usr/bin/env python3
import csv
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def as_float(value):
    try:
        if value == "" or value.lower() == "nan":
            return None
        v = float(value)
        return v if math.isfinite(v) else None
    except Exception:
        return None


def mean(values):
    values = [v for v in values if v is not None]
    return sum(values) / len(values) if values else None


def fmt(value):
    if value is None:
        return "NA"
    if abs(value) >= 1000 or (abs(value) > 0 and abs(value) < 1e-3):
        return f"{value:.4e}"
    return f"{value:.6f}"


def read_rows(path):
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def group_key(row):
    if "family" in row and "randomization" in row:
        return f"{row['family']}:{row['randomization']}"
    for key in ("variant", "layers", "config", "mode", "label", "stage"):
        if key in row and row[key] != "":
            if key == "layers" and "mode" in row:
                return f"{row[key]}:{row['mode']}"
            return row[key]
    return "all"


def summarize_file(path):
    rows = read_rows(path)
    if not rows:
        return []
    groups = defaultdict(list)
    for row in rows:
        groups[group_key(row)].append(row)
    numeric_cols = [
        "latency_ms",
        "logit_mae",
        "logit_mse",
        "h1_mae",
        "h2_mae",
        "product_error",
        "cross_error",
        "repeated_product_error",
        "mae",
        "mse",
        "accuracy",
        "max_abs_bias",
        "mean_bias",
        "std_bias",
        "mean_gain",
        "max_gain",
        "p95_gain",
        "precision_bits",
        "max_abs_error",
        "mean_signed_error",
        "levels_remaining",
        "score_mae",
        "score_max_abs",
        "softmax_mae",
        "top1_match",
        "abs_error",
        "rel_error",
        "error_corr",
    ]
    out = []
    for key, grows in sorted(groups.items()):
        success_col = "success" if "success" in grows[0] else None
        success = ""
        if success_col:
            ok = sum(1 for r in grows if r.get(success_col) in ("1", "true", "True"))
            success = f"{ok}/{len(grows)}"
        metrics = {}
        for col in numeric_cols:
            if col in grows[0]:
                metrics[col] = mean(as_float(r.get(col, "")) for r in grows)
        out.append((key, success, metrics))
    return out


def bound_summary(paths):
    rows = []
    for path in paths:
        ptxt = path.stem.replace("e8_bound_p", "")
        try:
            p = float(ptxt)
        except Exception:
            continue
        data = read_rows(path)
        if not data:
            continue
        product = [r for r in data if r.get("mode") == "product_safe"]
        if not product:
            product = data
        failures = sum(1 for r in product if r.get("success") not in ("1", "true", "True"))
        n = len(product)
        empirical = failures / n if n else None
        # Conservative observable proxies for the paper's post-hoc bound table.
        markov = min(1.0, (1.0 - p) / max(p, 1e-12))
        chebyshev = min(1.0, (1.0 - p) / max(4.0 * p, 1e-12))
        rows.append((p, n, empirical, markov, chebyshev))
    return sorted(rows)


def main():
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("/workspace/results/ccs2026")
    print("# CCS 2026 Experiment Summary")
    print()
    print(f"Result directory: `{root}`")
    print()
    csvs = sorted(root.glob("*.csv"))
    if not csvs:
        print("No CSV files found.")
        return
    for path in csvs:
        if path.name.startswith("e8_bound_"):
            continue
        print(f"## {path.name}")
        summary = summarize_file(path)
        if not summary:
            print("No rows.")
            print()
            continue
        metric_names = sorted({name for _, _, metrics in summary for name in metrics})
        print("| group | success | " + " | ".join(metric_names) + " |")
        print("|---|---:|" + "|".join(["---:" for _ in metric_names]) + "|")
        for key, success, metrics in summary:
            print("| " + key + " | " + success + " | " + " | ".join(fmt(metrics.get(m)) for m in metric_names) + " |")
        print()
    bound_rows = bound_summary(sorted(root.glob("e8_bound_p*.csv")))
    if bound_rows:
        print("## e8_bound_sweep")
        print("| p | n | empirical_failure | markov_proxy | chebyshev_proxy |")
        print("|---:|---:|---:|---:|---:|")
        for p, n, empirical, markov, chebyshev in bound_rows:
            print(f"| {p:.2f} | {n} | {fmt(empirical)} | {fmt(markov)} | {fmt(chebyshev)} |")
        print()


if __name__ == "__main__":
    main()
