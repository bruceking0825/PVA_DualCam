r"""Convert files in a directory to 8-bit grayscale BMP (requires Pillow).

Example:
    py cpp/convert_images_to_8bit_bmp.py --input-dir C:\CGS --output-dir C:\CGS\gray8
"""
import argparse
import struct
from pathlib import Path
from PIL import Image

EXTENSIONS = {".bmp", ".png", ".jpg", ".jpeg", ".tif", ".tiff", ".webp"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args()
    source_dir = args.input_dir.resolve()
    target_dir = args.output_dir.resolve()
    if not source_dir.is_dir():
        parser.error(f"Input directory does not exist: {source_dir}")
    if source_dir == target_dir:
        parser.error("Input and output directories must differ to protect source images")
    sources = sorted(p for p in source_dir.iterdir() if p.is_file() and p.suffix.lower() in EXTENSIONS)
    names = [f"{p.stem}.bmp".casefold() for p in sources]
    if len(names) != len(set(names)):
        parser.error("Multiple inputs would produce the same output BMP name")
    target_dir.mkdir(parents=True, exist_ok=True)
    converted = skipped = failed = 0
    for source in sources:
        target = target_dir / f"{source.stem}.bmp"
        if target.exists() and not args.overwrite:
            print(f"SKIP (exists): {target}")
            skipped += 1
            continue
        try:
            with Image.open(source) as image:
                if image.mode in {"RGBA", "LA"} or "transparency" in image.info:
                    rgba = image.convert("RGBA")
                    background = Image.new("RGBA", rgba.size, "white")
                    gray = Image.alpha_composite(background, rgba).convert("RGB").convert("L")
                else:
                    gray = image.convert("L")
                gray.save(target, "BMP")
            with target.open("rb") as bmp:
                bmp.seek(28)
                bpp = struct.unpack("<H", bmp.read(2))[0]
            if bpp != 8:
                raise RuntimeError(f"Output bit depth is {bpp}, expected 8")
            print(f"OK: {source} -> {target}")
            converted += 1
        except (OSError, ValueError, RuntimeError) as exc:
            print(f"ERROR: {source}: {exc}")
            failed += 1
    print(f"Done: {converted} converted, {skipped} skipped, {failed} failed.")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
