"""Generate the showroom's linear Radiance HDR source before baking it to NENV."""

import argparse
import math
from pathlib import Path

WIDTH, HEIGHT = 512, 256


def generate(output: Path) -> None:
    hdr = bytearray(
        f"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {HEIGHT} +X {WIDTH}\n".encode("ascii")
    )
    for y in range(HEIGHT):
        polar = math.pi * (y + 0.5) / HEIGHT
        for x in range(WIDTH):
            azimuth = math.tau * (x + 0.5) / WIDTH

            def light(center_azimuth: float, center_polar: float, sharpness: float) -> float:
                cosine = (
                    math.sin(polar) * math.sin(center_polar) * math.cos(azimuth - center_azimuth)
                    + math.cos(polar) * math.cos(center_polar)
                )
                return math.exp(sharpness * (cosine - 1))

            radiance = (
                0.15
                + 0.20 * (0.5 + 0.5 * math.cos(polar))
                + 1.65 * light(2.1, 0.64, 10)
                + 0.65 * light(5.3, 1.05, 7)
            )
            mantissa, exponent = math.frexp(radiance)
            channel = int(mantissa * 256)
            hdr.extend((channel, channel, channel, exponent + 128))
    output.write_bytes(hdr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    generate(parser.parse_args().output)
