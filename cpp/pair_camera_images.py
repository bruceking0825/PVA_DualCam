"""Rotate timestamp-matched camera images and combine them side by side.

Default input and output directories, resolved relative to this script::

    PVA_DualCam/pva_img/single
    PVA_DualCam/pva_img/pair

Input names must use ``imgA_YYYYMMDD_HHMMSS.png`` and
``imgB_YYYYMMDD_HHMMSS.png``. Camera A is placed on the left and camera B on
the right after both images are rotated 90 degrees counter-clockwise.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

from PIL import Image


FILE_PATTERN = re.compile(
    r"^img(?P<camera>[AB])_(?P<timestamp>\d{8}_\d{6})\.png$",
    re.IGNORECASE,
)


def project_root() -> Path:
    """Return the PVA_DualCam root when the script is stored under cpp/."""
    return Path(__file__).resolve().parent.parent


def parse_args() -> argparse.Namespace:
    root = project_root()
    parser = argparse.ArgumentParser(
        description=(
            "Pair imgA/imgB PNG files by timestamp, rotate both 90 degrees "
            "counter-clockwise, and place A left of B."
        )
    )
    parser.add_argument(
        "--input-dir",
        type=Path,
        default=root / "pva_img" / "single",
        help="Input directory (default: PVA_DualCam/pva_img/single).",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=root / "pva_img" / "pair",
        help="Output directory (default: PVA_DualCam/pva_img/pair).",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Overwrite pair_*.png files that already exist.",
    )
    return parser.parse_args()


def find_pairs(input_dir: Path) -> dict[str, dict[str, Path]]:
    """Group supported input files by their filename timestamp."""
    pairs: dict[str, dict[str, Path]] = defaultdict(dict)

    for image_path in sorted(input_dir.iterdir()):
        if not image_path.is_file():
            continue
        match = FILE_PATTERN.fullmatch(image_path.name)
        if match is None:
            continue

        camera = match.group("camera").upper()
        timestamp = match.group("timestamp")
        if camera in pairs[timestamp]:
            raise ValueError(
                f"Duplicate img{camera} image for timestamp {timestamp}: "
                f"{pairs[timestamp][camera].name}, {image_path.name}"
            )
        pairs[timestamp][camera] = image_path

    return dict(pairs)


def normalized_mode(left: Image.Image, right: Image.Image) -> str:
    """Choose one output color mode without silently discarding alpha."""
    if left.mode == right.mode:
        return left.mode
    if "A" in left.getbands() or "A" in right.getbands():
        return "RGBA"
    return "RGB"


def combine_pair(image_a_path: Path, image_b_path: Path, output_path: Path) -> tuple[int, int]:
    """Rotate one A/B pair counter-clockwise and save A left of B."""
    with Image.open(image_a_path) as source_a, Image.open(image_b_path) as source_b:
        # Pillow 的 ROTATE_90 是逆时针 90°，不会进行插值或改变原始像素值。
        image_a = source_a.transpose(Image.Transpose.ROTATE_90)
        image_b = source_b.transpose(Image.Transpose.ROTATE_90)

        if image_a.height != image_b.height:
            raise ValueError(
                "Rotated image heights do not match: "
                f"{image_a_path.name}={image_a.size}, "
                f"{image_b_path.name}={image_b.size}"
            )

        mode = normalized_mode(image_a, image_b)
        if image_a.mode != mode:
            image_a = image_a.convert(mode)
        if image_b.mode != mode:
            image_b = image_b.convert(mode)

        combined = Image.new(mode, (image_a.width + image_b.width, image_a.height))
        combined.paste(image_a, (0, 0))
        combined.paste(image_b, (image_a.width, 0))
        combined.save(output_path, format="PNG")
        return combined.size


def main() -> int:
    args = parse_args()
    input_dir = args.input_dir.resolve()
    output_dir = args.output_dir.resolve()

    if not input_dir.is_dir():
        print(f"Input directory does not exist: {input_dir}", file=sys.stderr)
        return 2

    output_dir.mkdir(parents=True, exist_ok=True)

    try:
        pairs = find_pairs(input_dir)
    except ValueError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2

    if not pairs:
        print(f"No matching imgA/imgB filenames found in: {input_dir}")
        return 1

    written = 0
    skipped = 0
    incomplete = 0
    failed = 0

    for timestamp, cameras in sorted(pairs.items()):
        missing = [camera for camera in ("A", "B") if camera not in cameras]
        if missing:
            print(
                f"Warning: skip {timestamp}; missing img{' and img'.join(missing)}.",
                file=sys.stderr,
            )
            incomplete += 1
            continue

        output_path = output_dir / f"pair_{timestamp}.png"
        if output_path.exists() and not args.overwrite:
            print(f"Skip existing: {output_path.name}")
            skipped += 1
            continue

        try:
            size = combine_pair(cameras["A"], cameras["B"], output_path)
        except (OSError, ValueError) as exc:
            print(f"Error: failed {timestamp}: {exc}", file=sys.stderr)
            failed += 1
            continue

        print(f"Saved: {output_path.name} ({size[0]}x{size[1]})")
        written += 1

    print(
        "Summary: "
        f"written={written}, skipped={skipped}, "
        f"incomplete={incomplete}, failed={failed}"
    )
    return 1 if incomplete or failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
