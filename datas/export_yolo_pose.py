"""Export a small, sequence-separated YOLO Pose dataset from the cached shard."""

import argparse
import json
import math
import random
import tarfile
from pathlib import Path

from prepare_stickers import resolve_shard


def pose_rows(annotation):
    width, height = annotation["image"]["width"], annotation["image"]["height"]
    rows = []
    if width <= 0 or height <= 0:
        return None
    for obj in annotation.get("objects", []):
        if obj.get("role") == "negative":
            continue
        corners = obj.get("corners")
        visibility = obj.get("visibility") or [2, 2, 2, 2]
        if (obj.get("role") == "ignore" or obj.get("corner_def") != "sjtu4"
                or obj.get("label_type") not in {"human", "human_checked"}
                or not corners or len(corners) != 4 or len(visibility) != 4
                or any(v not in {1, 2} for v in visibility)):
            return None
        points = [(x / width, y / height) for x, y in corners]
        if not all(math.isfinite(value) and 0 <= value <= 1 for point in points for value in point):
            return None
        xs, ys = zip(*points)
        x1, x2, y1, y2 = min(xs), max(xs), min(ys), max(ys)
        if x1 == x2 or y1 == y2:
            return None
        box = ((x1 + x2) / 2, (y1 + y2) / 2, x2 - x1, y2 - y1)
        keypoints = [f"{x:.8f} {y:.8f} {v}" for (x, y), v in zip(points, visibility, strict=True)]
        rows.append("0 " + " ".join([*(f"{n:.8f}" for n in box), *keypoints]))
    return rows


def write_split(destination, split, entries):
    images = destination / "images" / split
    labels = destination / "labels" / split
    images.mkdir(parents=True)
    labels.mkdir(parents=True)
    for key, image, rows in entries:
        (images / (key + ".jpg")).write_bytes(image)
        (labels / (key + ".txt")).write_text("\n".join(rows) + ("\n" if rows else ""), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--holdout", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True, help="New directory for the exported dataset")
    parser.add_argument("--train-images", type=int, default=192)
    archive_arg = parser.add_mutually_exclusive_group()
    archive_arg.add_argument("--archive", type=Path)
    archive_arg.add_argument("--download", action="store_true")
    args = parser.parse_args()
    if args.train_images < 1 or args.out.exists():
        parser.error("Use a positive --train-images and a new output directory")

    validation = []
    held_out = set()
    for path in sorted(args.holdout.glob("*.json")):
        annotation = json.loads(path.read_text(encoding="utf-8"))
        sequence = annotation["image"].get("sequence")
        if not sequence:
            parser.error(f"Missing validation sequence in {path}")
        held_out.add(sequence)
        rows = pose_rows(annotation)
        if rows is not None:
            validation.append((path.stem, path.with_suffix(".jpg").read_bytes(), rows))
    if not validation:
        parser.error("No usable validation images")

    with tarfile.open(resolve_shard(args.archive, args.download), "r:*") as tar:
        members = {Path(member.name).name: member for member in tar if member.isfile()}
        keys = sorted(name[:-5] for name in members if name.endswith(".json") and name[:-5] + ".jpg" in members)
        random.Random(42).shuffle(keys)
        train = []
        train_sequences = set()
        for key in keys:
            with tar.extractfile(members[key + ".json"]) as source:
                annotation = json.load(source)
            sequence = annotation["image"].get("sequence")
            if not sequence or sequence in held_out:
                continue
            rows = pose_rows(annotation)
            if rows is None:
                continue
            with tar.extractfile(members[key + ".jpg"]) as source:
                train.append((key, source.read(), rows))
            train_sequences.add(sequence)
            if len(train) == args.train_images:
                break
    if not train or not any(rows for _, _, rows in train):
        parser.error("No positive training examples")

    write_split(args.out, "train", train)
    write_split(args.out, "val", validation)
    dataset_path = json.dumps(args.out.resolve().as_posix(), ensure_ascii=False)
    (args.out / "dataset.yaml").write_text(
        f"path: {dataset_path}\ntrain: images/train\nval: images/val\nkpt_shape: [4, 3]\nnames:\n  0: plate\n",
        encoding="utf-8",
    )
    report = {
        "keypoint_order": ["LT", "LB", "RB", "RT"],
        "train_images": len(train), "val_images": len(validation),
        "train_objects": sum(len(rows) for _, _, rows in train),
        "val_objects": sum(len(rows) for _, _, rows in validation),
        "train_sequences": sorted(train_sequences), "holdout_sequences": sorted(held_out),
    }
    (args.out / "export.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{len(train)} train images, {len(validation)} validation images, {len(train_sequences)} training sequences")
    print(args.out / "dataset.yaml")


if __name__ == "__main__":
    main()
