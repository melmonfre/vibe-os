#!/usr/bin/env python3
import argparse
from pathlib import Path

from PIL import Image, ImageOps


RESAMPLING = getattr(Image, "Resampling", Image)
QUANTIZE = getattr(Image, "Quantize", Image)
DITHER = getattr(Image, "Dither", Image)


def build_runtime_png(input_path: Path, output_path: Path, width: int, height: int) -> None:
    with Image.open(input_path) as image:
        rgb = image.convert("RGB")
        fitted = ImageOps.fit(rgb, (width, height), method=RESAMPLING.LANCZOS, centering=(0.5, 0.5))
        quantized = fitted.quantize(colors=256, method=QUANTIZE.MEDIANCUT, dither=DITHER.FLOYDSTEINBERG)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        quantized.save(output_path, format="PNG", optimize=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build a compact runtime PNG asset.")
    parser.add_argument("--input", required=True, help="Input image path")
    parser.add_argument("--output", required=True, help="Output PNG path")
    parser.add_argument("--width", type=int, required=True, help="Target width")
    parser.add_argument("--height", type=int, required=True, help="Target height")
    args = parser.parse_args()

    build_runtime_png(Path(args.input), Path(args.output), args.width, args.height)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
