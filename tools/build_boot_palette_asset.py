#!/usr/bin/env python3
import argparse
from pathlib import Path
from typing import List, Tuple

from PIL import Image, ImageOps


EGA16 = [
    (0, 0, 0),
    (0, 0, 170),
    (0, 170, 0),
    (0, 170, 170),
    (170, 0, 0),
    (170, 0, 170),
    (170, 85, 0),
    (170, 170, 170),
    (85, 85, 85),
    (85, 85, 255),
    (85, 255, 85),
    (85, 255, 255),
    (255, 85, 85),
    (255, 85, 255),
    (255, 255, 85),
    (255, 255, 255),
]

RESAMPLING = getattr(Image, "Resampling", Image)
RESAMPLERS = {
    "nearest": RESAMPLING.NEAREST,
    "box": RESAMPLING.BOX,
    "bilinear": RESAMPLING.BILINEAR,
    "bicubic": RESAMPLING.BICUBIC,
    "lanczos": RESAMPLING.LANCZOS,
}


def build_palette() -> List[Tuple[int, int, int]]:
    palette = list(EGA16)
    for index in range(16, 256):
        palette.append(
            (
                ((index >> 5) & 0x07) * 255 // 7,
                ((index >> 2) & 0x07) * 255 // 7,
                (index & 0x03) * 255 // 3,
            )
        )
    return palette


def nearest_palette_index(palette: List[Tuple[int, int, int]], rgb: Tuple[int, int, int]) -> int:
    r, g, b = rgb
    best_index = 0
    best_distance = None

    for index, (pr, pg, pb) in enumerate(palette):
        dr = r - pr
        dg = g - pg
        db = b - pb
        distance = (dr * dr) + (dg * dg) + (db * db)
        if best_distance is None or distance < best_distance:
            best_distance = distance
            best_index = index

    return best_index


def convert_image(input_path: Path, output_path: Path, width: int, height: int, resample: str) -> None:
    palette = build_palette()

    with Image.open(input_path) as image:
        rgba = image.convert("RGBA")
        fitted = ImageOps.fit(rgba, (width, height), method=RESAMPLERS[resample], centering=(0.5, 0.5))
        pixels = fitted.load()

        output = bytearray(width * height)
        for y in range(height):
            for x in range(width):
                r, g, b, a = pixels[x, y]
                if a < 255:
                    r = (r * a) // 255
                    g = (g * a) // 255
                    b = (b * a) // 255
                output[(y * width) + x] = nearest_palette_index(palette, (r, g, b))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(output)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build a raw palettized boot image asset.")
    parser.add_argument("--input", required=True, help="Input image path")
    parser.add_argument("--output", required=True, help="Output binary path")
    parser.add_argument("--width", type=int, required=True, help="Target width in pixels")
    parser.add_argument("--height", type=int, required=True, help="Target height in pixels")
    parser.add_argument(
        "--resample",
        choices=sorted(RESAMPLERS.keys()),
        default="box",
        help="Resize filter to use before palette conversion",
    )
    args = parser.parse_args()

    convert_image(Path(args.input), Path(args.output), args.width, args.height, args.resample)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
