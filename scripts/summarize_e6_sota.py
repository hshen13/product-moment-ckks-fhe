#!/usr/bin/env python3
import csv
import itertools
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def exact_wilcoxon_p(diffs, alternative):
    values = [d for d in diffs if abs(d) > 0]
    n = len(values)
    if n == 0:
        return 1.0
    order = sorted(range(n), key=lambda i: abs(values[i]))
    ranks = [0.0] * n
    i = 0
    while i < n:
        j = i + 1
        while j < n and abs(values[order[j]]) == abs(values[order[i]]):
            j += 1
        avg = (i + 1 + j) / 2.0
        for k in range(i, j):
            ranks[order[k]] = avg
        i = j
    observed = sum(ranks[i] for i, d in enumerate(values) if d > 0)
    all_stats = []
    for signs in itertools.product([0, 1], repeat=n):
        all_stats.append(sum(ranks[i] for i, sign in enumerate(signs) if sign))
    if alternative == "less":
        return sum(x <= observed + 1e-12 for x in all_stats) / len(all_stats)
    return sum(x >= observed - 1e-12 for x in all_stats) / len(all_stats)


def mean(rows, key):
    return statistics.mean(float(r[key]) for r in rows)


def main():
    csv_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("/workspace/results/ccs2026/e6_sota_bootstrap.csv")
    out_path = Path(sys.argv[2]) if len(sys.argv) > 2 else csv_path.with_suffix(".md")
    with csv_path.open(newline="") as f:
        rows = [r for r in csv.DictReader(f) if r.get("success") == "1"]

    by = defaultdict(list)
    for row in rows:
        row["seed"] = int(row["seed"])
        by[(row["family"], row["randomization"])].append(row)

    lines = [
        "# E6 SOTA Bootstrap Composition Results",
        "",
        f"Input CSV: `{csv_path}`",
        "",
        "## Aggregate",
        "",
        "| Family | Randomization | Success | Mean latency ms | Mean max abs err | Mean MAE | Mean precision bits |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    families = sorted({r["family"] for r in rows})
    for family in families:
        for randomization in ("none", "product_coupled"):
            group = by[(family, randomization)]
            if not group:
                continue
            lines.append(
                f"| {family} | {randomization} | {len(group)}/{len(group)} | "
                f"{mean(group, 'latency_ms'):.2f} | {mean(group, 'max_abs_error'):.3e} | "
                f"{mean(group, 'mae'):.3e} | {mean(group, 'precision_bits'):.2f} |"
            )

    lines += [
        "",
        "## Paired Product-Coupled Effect",
        "",
        "Ratios are product_coupled / deterministic. For error and latency, lower is better; for precision bits, higher is better. p-values are exact paired Wilcoxon one-sided tests.",
        "",
        "| Family | Metric | Ratio | Product wins | p-value | Significant at 0.05? |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    significant = []
    for family in families:
        det = {r["seed"]: r for r in by[(family, "none")]}
        prod = {r["seed"]: r for r in by[(family, "product_coupled")]}
        seeds = sorted(det.keys() & prod.keys())
        for metric in ("max_abs_error", "mae", "latency_ms", "precision_bits"):
            dvals = [float(det[s][metric]) for s in seeds]
            pvals = [float(prod[s][metric]) for s in seeds]
            lower_better = metric != "precision_bits"
            diffs = [pvals[i] - dvals[i] for i in range(len(seeds))]
            p_value = exact_wilcoxon_p(diffs, "less" if lower_better else "greater")
            wins = sum((pvals[i] < dvals[i]) if lower_better else (pvals[i] > dvals[i]) for i in range(len(seeds)))
            ratio = statistics.mean(pvals) / statistics.mean(dvals)
            is_sig = p_value < 0.05
            if is_sig:
                significant.append(f"{family}/{metric}")
            lines.append(
                f"| {family} | {metric} | {ratio:.4f} | {wins}/{len(seeds)} | "
                f"{p_value:.4f} | {'yes' if is_sig else 'no'} |"
            )

    lines += ["", "## Interpretation", ""]
    if significant:
        lines.append("Product-coupled reaches p < 0.05 for: " + ", ".join(significant) + ".")
    else:
        lines.append("Product-coupled does not reach p < 0.05 on these metrics; claim compatibility rather than improvement.")
    lines.append(
        "For Composite / Meta-BTS rows, interpret a non-significant result as stable composition, not as evidence of added precision."
    )
    out_path.write_text("\n".join(lines) + "\n")
    print(out_path)


if __name__ == "__main__":
    main()
