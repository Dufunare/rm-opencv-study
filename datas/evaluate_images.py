"""Run the C++ study program on annotated images and summarize its outputs."""

import argparse
import csv
import json
import math
import statistics
import subprocess
from pathlib import Path


POINTS = ("lt", "lb", "rb", "rt")


def normalized_error(corners, row):
    predicted = [(float(row[f"{p}_x"]), float(row[f"{p}_y"])) for p in POINTS]
    height = (math.dist(corners[0], corners[1]) + math.dist(corners[2], corners[3])) / 2
    error = statistics.mean(math.dist(a, b) for a, b in zip(corners, predicted, strict=True))
    return error / max(1, height), error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--samples", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--number-model", type=Path, default=Path("out/models/stickers.yml.gz"))
    args = parser.parse_args()
    sidecars = sorted(args.samples.glob("*.json"))
    if not sidecars:
        parser.error("--samples must contain sidecar JSON files")

    fieldnames = ["image", "reference_count", "predicted_count", "matched", "color_correct",
                  "sticker_labeled_on_matches", "sticker_correct", "gray_references"]
    totals = dict.fromkeys(fieldnames[1:], 0)
    errors = []
    reprojections = []
    args.out.mkdir(parents=True, exist_ok=True)
    with (args.out / "per_image.csv").open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=fieldnames)
        writer.writeheader()
        for sidecar in sidecars:
            destination = args.out / sidecar.stem
            run = subprocess.run(
                [args.exe.resolve(), "--image", sidecar.with_suffix(".jpg"), "--out", destination,
                 "--number-model", args.number_model, "--no-gui"], capture_output=True, encoding="utf-8"
            )
            if run.returncode:
                raise RuntimeError(f"{sidecar.name}: {run.stderr.strip()}")
            objects = [obj for obj in json.loads(sidecar.read_text(encoding="utf-8"))["objects"]
                       if obj.get("corners") and obj.get("corner_def") == "sjtu4"
                       and obj.get("role") not in {"ignore", "negative"}]
            with (destination / "detections.csv").open(newline="", encoding="utf-8") as results:
                rows = list(csv.DictReader(results))
            lit = [obj for obj in objects if obj.get("color") in {"red", "blue"}]
            possible = []
            for i, obj in enumerate(lit):
                for j, row in enumerate(rows):
                    ratio, pixels = normalized_error(obj["corners"], row)
                    if ratio <= 0.35:
                        possible.append((ratio, i, j, pixels))

            references, predictions = set(), set()
            summary = dict.fromkeys(fieldnames[1:], 0)
            summary.update(reference_count=len(lit), predicted_count=len(rows), gray_references=len(objects) - len(lit))
            for _, i, j, pixels in sorted(possible):
                if i in references or j in predictions:
                    continue
                references.add(i)
                predictions.add(j)
                errors.append(pixels)
                obj, row = lit[i], rows[j]
                summary["matched"] += 1
                summary["color_correct"] += row["color"] == obj["color"]
                if obj.get("sticker"):
                    summary["sticker_labeled_on_matches"] += 1
                    summary["sticker_correct"] += row["sticker"] == obj["sticker"]
                if row["pose_ok"] == "1":
                    reprojections.append(float(row["reproj_rms_px"]))
            writer.writerow(dict(image=sidecar.stem, **summary))
            for name, value in summary.items():
                totals[name] += value

    report = {"images": len(sidecars), **totals,
              "mean_corner_error_px_on_matches": round(statistics.mean(errors), 3) if errors else None,
              "median_reprojection_rms_px_on_matches": round(statistics.median(reprojections), 3) if reprojections else None}
    (args.out / "summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
