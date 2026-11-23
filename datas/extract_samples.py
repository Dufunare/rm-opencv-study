import argparse
import json
import random
import tarfile
from pathlib import Path

from prepare_stickers import resolve_shard

parser = argparse.ArgumentParser(description="Extract the fixed 20-image rmcv/armor sample.")
parser.add_argument("--out", type=Path, default=Path(__file__).parent / "samples/mixed_armor")
archive_arg = parser.add_mutually_exclusive_group()
archive_arg.add_argument("--archive", type=Path)
archive_arg.add_argument("--download", action="store_true")
args = parser.parse_args()
archive_path = resolve_shard(args.archive, args.download)

output_dir = args.out
output_dir.mkdir(parents=True, exist_ok=True)

with tarfile.open(archive_path, "r:*") as tar:
    members = {
        Path(m.name).name: m
        for m in tar.getmembers()
        if m.isfile()
    }

    # 找到 JPG 和 JSON 都存在的样本
    keys = sorted(
        name[:-4]
        for name in members
        if name.endswith(".jpg")
        and name[:-4] + ".json" in members
    )

    print(f"Available image/annotation pairs: {len(keys)}")

    # 固定随机种子，每次抽取相同的样本
    selected = random.Random(42).sample(
        keys, min(20, len(keys))
    )

    for key in selected:
        image = tar.extractfile(members[key + ".jpg"])
        annotation = tar.extractfile(members[key + ".json"])

        if image is None or annotation is None:
            continue

        image_bytes = image.read()
        annotation_bytes = annotation.read()
        label = json.loads(annotation_bytes)

        (output_dir / f"{key}.jpg").write_bytes(image_bytes)
        (output_dir / f"{key}.json").write_bytes(annotation_bytes)

        objects = label.get("objects", [])
        print(f"{key}: {len(objects)} objects")

print(f"Samples saved to: {output_dir.resolve()}")
