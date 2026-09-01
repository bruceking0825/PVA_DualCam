"""Extract two column ranges from one image and combine them side by side.

Default slices:
    image[:, 1500:(1500 + 1700)]
    image[:, (5120 + 1500):(5120 + 1500 + 1700)]
"""

from __future__ import annotations

import argparse
from pathlib import Path

import cv2
import numpy as np


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract two camera regions from a combined image."
    )
    parser.add_argument("input", type=Path, help="Input image path.")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="Output image path. Defaults to '<input_stem>_combined<input_suffix>'.",
    )
    parser.add_argument(
        "--left-start",
        type=int,
        default=1500,
        help="Start column for the left crop.",
    )
    parser.add_argument(
        "--right-start",
        type=int,
        default=5120 + 1500,
        help="Start column for the right crop.",
    )
    parser.add_argument(
        "--width",
        type=int,
        default=1700,
        help="Crop width for each camera region.",
    )
    return parser.parse_args()


def validate_range(image: np.ndarray, start: int, width: int, name: str) -> None:
    if start < 0:
        raise ValueError(f"{name} start column must be >= 0, got {start}.")
    if width <= 0:
        raise ValueError(f"Crop width must be > 0, got {width}.")

    end = start + width
    image_width = image.shape[1]
    if end > image_width:
        raise ValueError(
            f"{name} crop [{start}:{end}] exceeds image width {image_width}."
        )


def default_output_path(input_path: Path) -> Path:
    return input_path.with_name(f"{input_path.stem}_combined{input_path.suffix}")


def main() -> int:
    args = parse_args()
    input_path = args.input
    output_path = args.output or default_output_path(input_path)

    image = cv2.imread(str(input_path), cv2.IMREAD_UNCHANGED)
    if image is None:
        raise FileNotFoundError(f"Failed to read image: {input_path}")

    validate_range(image, args.left_start, args.width, "left")
    validate_range(image, args.right_start, args.width, "right")

    left = image[:, args.left_start : args.left_start + args.width]
    right = image[:, args.right_start : args.right_start + args.width]
    combined = np.hstack((left, right))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    ok = cv2.imwrite(str(output_path), combined)
    if not ok:
        raise OSError(f"Failed to write output image: {output_path}")

    print(f"Saved: {output_path}")
    print(f"Input shape: {image.shape}")
    print(f"Output shape: {combined.shape}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
