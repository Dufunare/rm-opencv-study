"""Make a bounded, sequence-separated HOG training manifest from rmcv/armor."""

import argparse
import json
import math
import random
import tarfile
from collections import Counter, defaultdict
from pathlib import Path

from huggingface_hub import hf_hub_download
from PIL import Image


REVISION = "b1ea191eb475102b31e6890db46a210c8c2982d0"
SHARD = "data/mixed_armor/train/mixed_armor-train-00000.tar"
LABELS = {"1", "2", "3", "4", "5", "sentry", "outpost", "base"}


def resolve_shard(archive, download):
    return archive or hf_hub_download(
        "rmcv/armor", SHARD, repo_type="dataset", revision=REVISION, local_files_only=not download
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--holdout", type=Path, required=True, help="Directory of evaluation sidecar JSONs")
    parser.add_argument("--out", type=Path, required=True)
    archive_arg = parser.add_mutually_exclusive_group()
    archive_arg.add_argument("--archive", type=Path)
    archive_arg.add_argument("--download", action="store_true", help="Allow downloading the pinned tar shard")
    args = parser.parse_args()

    held_out = {json.loads(path.read_text(encoding="utf-8"))["image"]["sequence"]
                for path in args.holdout.glob("*.json")}
    if not held_out or None in held_out:
        parser.error("--holdout must contain sample annotations with sequence IDs")
    archive = resolve_shard(args.archive, args.download)
    crops = args.out / "crops"
    crops.mkdir(parents=True, exist_ok=True)
    rng = random.Random(42)
    used = Counter()
    with tarfile.open(archive, "r:*") as tar:
        members = {Path(member.name).name: member for member in tar if member.isfile()}
        by_class = defaultdict(list)
        for filename in sorted(members):
            if not filename.endswith(".json") or filename[:-5] + ".jpg" not in members:
                continue
            with tar.extractfile(members[filename]) as annotation:
                data = json.load(annotation)
            sequence = data["image"].get("sequence")
            if not sequence or sequence in held_out:
                continue
            for index, obj in enumerate(data.get("objects", [])):
                corners = obj.get("corners")
                sticker = obj.get("sticker")
                visibility = obj.get("visibility")
                if (sticker not in LABELS or obj.get("label_type") not in {"human", "human_checked"}
                        or obj.get("corner_def") != "sjtu4" or not corners or len(corners) != 4
                        or obj.get("role") in {"ignore", "negative"}
                        or (visibility and min(visibility) < 2)
                        or min(math.dist(corners[0], corners[1]), math.dist(corners[2], corners[3])) < 6):
                    continue
                by_class[sticker].append((filename[:-5], sequence, index, corners))

        with (args.out / "train.tsv").open("w", encoding="utf-8", newline="\n") as manifest:
            for sticker, candidates in sorted(by_class.items()):
                rng.shuffle(candidates)
                per_sequence = Counter()
                for key, sequence, index, corners in candidates:
                    if used[sticker] >= 200:
                        break
                    if per_sequence[sequence] >= 30:
                        continue
                    xs, ys = zip(*corners)
                    height = max(math.dist(corners[0], corners[1]), math.dist(corners[2], corners[3]))
                    margin = math.ceil(2 * height)
                    with tar.extractfile(members[key + ".jpg"]) as image_file:
                        with Image.open(image_file) as image:
                            x = max(0, math.floor(min(xs)) - margin)
                            y = max(0, math.floor(min(ys)) - margin)
                            box = (x, y, min(image.width, math.ceil(max(xs)) + margin),
                                   min(image.height, math.ceil(max(ys)) + margin))
                            crop_name = f"{key}_{index}.png"
                            image.crop(box).convert("RGB").save(crops / crop_name)
                    coords = " ".join(f"{number:.4f}" for px, py in corners for number in (px - x, py - y))
                    manifest.write(f"crops/{crop_name}\t{sticker}\t{coords}\n")
                    used[sticker] += 1
                    per_sequence[sequence] += 1

    absent = LABELS - used.keys()
    print(f"Holdout sequences: {', '.join(sorted(held_out))}")
    print(f"Training examples by class: {dict(sorted(used.items()))}")
    if absent:
        print(f"Classes absent after filtering: {', '.join(sorted(absent))}")
    print(args.out / "train.tsv")


if __name__ == "__main__":
    main()
