"""Write the reference reflection test's fixed Hosek–Wilkie sky as Radiance RGBE."""

import argparse
import math
from pathlib import Path

# RGB A–I and Z evaluated from the pinned dwSampleFramework RGB tables.
# Turbidity 4, albedo 0.1, sun elevation 30 degrees.
COEFFICIENTS = (
    (-1.0689887941, -.1457726498, .7813792702, 1.5860648744, -2.9132230679,
     .8420151825, .3091128304, .6869642962, 1.3570561626, 9.1868411495),
    (-1.0776682273, -.1670659326, .6529928321, 1.9646937320, -3.7557022026,
     .7645803285, .1965158506, .6806296244, 2.2001568636, 11.6328756446),
    (-1.0963136328, -.2450787726, .4974454419, .0048392126, -.7678662739,
     .5883081539, .0987434191, .6600232455, 2.2932353655, 14.4119683253),
)
SUN = (math.sin(math.radians(35)) * math.sqrt(.75), .5,
       -math.cos(math.radians(35)) * math.sqrt(.75))


def radiance(coefficients, height, cosine):
    a, b, c, d, e, f, g, h, i, z = coefficients
    chi = (1 + cosine**2) / (1 + h**2 - 2 * cosine * h)**1.5
    return z * (1 + a * math.exp(b / (height + .01))) * (
        c + d * math.exp(e * math.acos(cosine)) + f * cosine**2 + g * chi + i * math.sqrt(height)
    )


def bake(output):
    width, height = 512, 256
    sun_rgb = [radiance(channel, SUN[1], 1) for channel in COEFFICIENTS]
    normalization = 1.15 / sum(a * b for a, b in zip(sun_rgb, (.2126, .7152, .0722)))
    with output.open("wb") as stream:
        stream.write(f"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {height} +X {width}\n".encode())
        for y in range(height):
            theta = math.pi * (y + .5) / height
            row = [bytearray() for _ in range(4)]
            for x in range(width):
                phi = 2 * math.pi * ((x + .5) / width - .5)
                direction = (math.cos(phi) * math.sin(theta), math.cos(theta),
                             math.sin(phi) * math.sin(theta))
                cosine = min(1, max(0, sum(a * b for a, b in zip(direction, SUN))))
                rgb = [max(0, radiance(c, max(0, direction[1]), cosine) * normalization)
                       for c in COEFFICIENTS]
                peak = max(rgb)
                if peak > 0:
                    mantissa, exponent = math.frexp(peak)
                    values = [int(c * mantissa * 256 / peak) for c in rgb] + [exponent + 128]
                else:
                    values = [0, 0, 0, 0]
                for channel, value in zip(row, values):
                    channel.append(value)
            stream.write(bytes((2, 2, width >> 8, width & 255)))
            for channel in row:
                for offset in range(0, width, 128):
                    chunk = channel[offset:offset + 128]
                    stream.write(bytes((len(chunk),)) + chunk)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    bake(parser.parse_args().output)
