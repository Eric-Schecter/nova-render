#!/usr/bin/env python3
"""Convert planar RGB OpenEXR to Radiance RGBE without clipping HDR values."""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess


def convert(source, target):
    if source.resolve() == target.resolve():
        raise ValueError("Input and output must be different files")
    metadata = subprocess.check_output([
        "ffprobe", "-v", "error", "-select_streams", "v:0",
        "-show_entries", "stream=width,height,pix_fmt", "-of", "json", str(source),
    ])
    stream, = json.loads(metadata)["streams"]
    width, height, fmt = stream["width"], stream["height"], stream["pix_fmt"]
    # Avoid swscale's format conversion. It can clamp HDR float channels to 1.
    formats = {"gbrpf16le": "e", "gbrapf16le": "e", "gbrpf32le": "f", "gbrapf32le": "f"}
    sample = formats.get(fmt)
    if not sample or not 8 <= width <= 32767:
        raise ValueError(f"Unsupported EXR layout: {width} x {height}, {fmt}")
    pixels = subprocess.check_output([
        "ffmpeg", "-v", "error", "-i", str(source), "-map", "0:v:0",
        "-frames:v", "1", "-pix_fmt", "+" + fmt, "-f", "rawvideo", "pipe:1",
    ])
    plane_bytes = width * height * struct.calcsize("<" + sample)
    if len(pixels) != plane_bytes * (4 if fmt.startswith("gbra") else 3):
        raise ValueError("Unexpected EXR pixel buffer length")
    planes = [struct.iter_unpack("<" + sample, memoryview(pixels)[i * plane_bytes:(i + 1) * plane_bytes])
              for i in (2, 0, 1)]
    peak = 0.0
    with target.open("wb") as out:
        out.write(f"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {height} +X {width}\n".encode())
        for _ in range(height):
            scanline = [bytearray() for _ in range(4)]
            for _ in range(width):
                rgb = [max(next(channel)[0], 0) for channel in planes]
                maximum = max(rgb)
                if not all(map(math.isfinite, rgb)) or maximum >= 2 ** 127:
                    raise ValueError("Radiance must be finite and representable as RGBE")
                peak = max(peak, maximum)
                exponent = math.frexp(maximum)[1] if maximum > 1e-32 else 0
                scale = math.ldexp(256.0, -exponent) if maximum > 1e-32 else 0
                for component, value in zip(scanline, rgb):
                    component.append(int(value * scale))
                scanline[3].append(exponent + 128 if scale else 0)
            out.write(bytes((2, 2, width >> 8, width & 255)))
            for channel in scanline:
                for offset in range(0, width, 128):
                    chunk = channel[offset:offset + 128]
                    out.write(bytes((len(chunk),)))
                    out.write(chunk)
    print(f"{width} x {height}, peak linear radiance {peak:g}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("target", type=Path)
    args = parser.parse_args()
    convert(args.source, args.target)
