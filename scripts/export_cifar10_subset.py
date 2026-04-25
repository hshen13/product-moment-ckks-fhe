#!/usr/bin/env python3
import csv
import tarfile
import urllib.request
from pathlib import Path

from PIL import Image


URL = "https://www.cs.toronto.edu/~kriz/cifar-10-binary.tar.gz"
LABELS = [
    "airplane",
    "automobile",
    "bird",
    "cat",
    "deer",
    "dog",
    "frog",
    "horse",
    "ship",
    "truck",
]


def main():
    dataset_dir = Path("/workspace/datasets")
    repo_dir = Path("/workspace/LowMemoryFHEResNet20")
    tar_path = dataset_dir / "cifar-10-binary.tar.gz"
    extracted = dataset_dir / "cifar-10-batches-bin"
    out_dir = repo_dir / "inputs" / "cifar10_subset"
    dataset_dir.mkdir(parents=True, exist_ok=True)
    out_dir.mkdir(parents=True, exist_ok=True)

    if not tar_path.exists():
        urllib.request.urlretrieve(URL, tar_path)
    if not extracted.exists():
        with tarfile.open(tar_path, "r:gz") as tar:
            tar.extractall(dataset_dir)

    data = (extracted / "test_batch.bin").read_bytes()
    record_size = 1 + 3072
    per_class = {i: 0 for i in range(10)}
    manifest = []
    selected = 0
    for idx in range(len(data) // record_size):
        offset = idx * record_size
        label = data[offset]
        if per_class[label] >= 1:
            continue
        raw = data[offset + 1 : offset + record_size]
        pixels = [(raw[i], raw[1024 + i], raw[2048 + i]) for i in range(1024)]
        image = Image.new("RGB", (32, 32))
        image.putdata(pixels)
        name = f"{selected:02d}_{LABELS[label]}_testidx{idx}.png"
        image.save(out_dir / name)
        manifest.append(
            {
                "path": f"inputs/cifar10_subset/{name}",
                "label": label,
                "class": LABELS[label],
                "test_index": idx,
            }
        )
        per_class[label] += 1
        selected += 1
        if selected == 10:
            break

    with (out_dir / "manifest.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["path", "label", "class", "test_index"])
        writer.writeheader()
        writer.writerows(manifest)

    print(out_dir)


if __name__ == "__main__":
    main()
