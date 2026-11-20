"""Prepare a sky-only display panorama from the original UE Radiance export."""

import argparse
import math
import re
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    hdr = args.input.read_bytes()
    header = re.match(rb"#\?RADIANCE\r?\nFORMAT=32-bit_rle_rgbe\r?\n\r?\n-Y (\d+) \+X (\d+)\r?\n", hdr)
    if not header:
        raise SystemExit("Expected the raw Radiance export from Unreal")
    height, width = map(int, header.groups())
    pixels = hdr[header.end():]
    stride = width * 4
    if height < 2 or width < 2 or len(pixels) != stride * height:
        raise SystemExit("Unexpected raw RGBE panorama dimensions")

    # Keep the original sky up to 4.5 degrees above the horizon. Below that,
    # taper a short mirror of the sky into the horizon's average linear radiance.
    cutoff = int(height * .475)
    blend_rows = max(1, int(height * .06))
    def decode_row(row):
        sky_row = pixels[row * stride:(row + 1) * stride]
        colors = []
        for i in range(0, stride, 4):
            exponent = sky_row[i + 3]
            scale = math.ldexp(1., exponent - 136) if exponent else 0.
            colors.append(tuple(sky_row[i + channel] * scale for channel in range(3)))
        return colors

    colors = decode_row(cutoff)
    average = tuple(sum(pixel[k] for pixel in colors) / width for k in range(3))

    def rgbe(rgb):
        value = max(rgb)
        if value < 1e-32:
            return (0, 0, 0, 0)
        mantissa, exponent = math.frexp(value)
        scale = mantissa * 256 / value
        return (*[min(255, int(c * scale)) for c in rgb], exponent + 128)

    with args.output.open("wb") as out:
        out.write(hdr[:header.end() + (cutoff + 1) * stride])
        for row in range(cutoff + 1, min(height, cutoff + blend_rows)):
            colors = decode_row(cutoff - (row - cutoff))
            t = (row - cutoff) / blend_rows
            t = t * t * (3 - 2 * t)
            out.write(bytes(v for color in colors for v in rgbe(
                tuple(a + (b - a) * t for a, b in zip(color, average))
            )))
        out.write(bytes(rgbe(average)) * width * max(0, height - cutoff - blend_rows))


if __name__ == "__main__":
    main()
