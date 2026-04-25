#!/usr/bin/env python3
import argparse
import csv
import math
import re
from pathlib import Path


ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
OUTPUT_RE = re.compile(r"Output:\s*\[([^\]]+)\]", re.S)
PRED_RE = re.compile(r"The index of max element is\s+([0-9]+)")
TIME_RE = re.compile(r"WALL_SECONDS=([0-9.]+)")


def clean(text):
    return ANSI_RE.sub("", text)


def parse_log(log_path, time_path):
    row = {
        "success": 0,
        "pred_label": "",
        "pred_class": "",
        "latency_sec": "",
        "logits": "",
        "error": "",
    }
    if time_path.exists():
        m = TIME_RE.search(time_path.read_text(errors="replace"))
        if m:
            row["latency_sec"] = m.group(1)
    if not log_path.exists():
        row["error"] = "missing log"
        return row

    text = clean(log_path.read_text(errors="replace"))
    out = OUTPUT_RE.search(text)
    pred = PRED_RE.search(text)
    if out and pred:
        vals = [float(x.strip()) for x in out.group(1).replace("\n", " ").split(",") if x.strip()]
        row["success"] = 1
        row["pred_label"] = int(pred.group(1))
        row["logits"] = " ".join(f"{x:.12g}" for x in vals)
        return row

    tail = " ".join(text.splitlines()[-8:])
    row["error"] = tail[:300]
    return row


def mae(xs, ys):
    if len(xs) != len(ys) or not xs:
        return ""
    return sum(abs(a - b) for a, b in zip(xs, ys)) / len(xs)


def parse_logits(s):
    if not s:
        return []
    return [float(x) for x in s.split()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--log-dir", required=True)
    ap.add_argument("--output", required=True)
    args = ap.parse_args()

    log_dir = Path(args.log_dir)
    classes = {
        0: "airplane",
        1: "automobile",
        2: "bird",
        3: "cat",
        4: "deer",
        5: "dog",
        6: "frog",
        7: "horse",
        8: "ship",
        9: "truck",
    }

    rows = []
    det_logits = {}
    with open(args.manifest, newline="") as f:
        for item in csv.DictReader(f):
            stem = Path(item["path"]).stem
            true_label = int(item["label"])
            for variant in ("deterministic", "product_safe", "independent"):
                parsed = parse_log(
                    log_dir / f"{stem}_{variant}.log",
                    log_dir / f"{stem}_{variant}.time",
                )
                pred_label = parsed["pred_label"]
                pred_class = classes.get(pred_label, "") if isinstance(pred_label, int) else ""
                out = {
                    "image": Path(item["path"]).name,
                    "test_index": item["test_index"],
                    "true_label": true_label,
                    "true_class": item["class"],
                    "variant": variant,
                    **parsed,
                    "pred_class": pred_class,
                    "correct": int(parsed["success"] and pred_label == true_label),
                    "logit_mae_vs_det": "",
                }
                if variant == "deterministic" and parsed["success"]:
                    det_logits[stem] = parse_logits(parsed["logits"])
                rows.append((stem, out))

    final_rows = []
    for stem, row in rows:
        if row["variant"] != "deterministic" and row["success"] and stem in det_logits:
            val = mae(parse_logits(row["logits"]), det_logits[stem])
            if val != "":
                row["logit_mae_vs_det"] = f"{val:.12g}"
        final_rows.append(row)

    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "image",
        "test_index",
        "true_label",
        "true_class",
        "variant",
        "success",
        "pred_label",
        "pred_class",
        "correct",
        "latency_sec",
        "logit_mae_vs_det",
        "logits",
        "error",
    ]
    with open(args.output, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for row in final_rows:
            writer.writerow({k: row.get(k, "") for k in fields})


if __name__ == "__main__":
    main()
