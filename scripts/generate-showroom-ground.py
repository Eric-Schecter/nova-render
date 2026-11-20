"""Generate CarConcept's stone paving and tire contact texture."""

import json
import math
import struct
import zlib
from pathlib import Path

SIZE = 1024
HALF_FLOOR = 18
# Tread contact centers in showroom world X/Z after the CarConcept instance translation.
WHEEL_CENTERS = ((0.981, 1.247), (-0.966, 1.236), (1.019, -1.553), (-0.976, -1.541))


def png_chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def paving_texture():
    """Return sRGB stone variation, joints and tire contact attenuation."""
    pixels = bytearray()
    angle = math.radians(12)
    cos_angle, sin_angle = math.cos(angle), math.sin(angle)
    joint_width = 0.026
    for row in range(SIZE):
        pixels.append(0)
        z = ((row + 0.5) / SIZE * 2 - 1) * HALF_FLOOR
        for col in range(SIZE):
            x = ((col + 0.5) / SIZE * 2 - 1) * HALF_FLOOR
            shade = 0
            if abs(x) < 2.1 and abs(z) < 2.6:
                for wheel_x, wheel_z in WHEEL_CENTERS:
                    dx, dz = x - wheel_x, z - wheel_z
                    inner = math.exp(-((dx / 0.18) ** 2) - (dz / 0.23) ** 2)
                    outer = math.exp(-((dx / 0.33) ** 2) - (dz / 0.32) ** 2)
                    shade = max(shade, 0.55 * inner + 0.18 * outer)
            u = x * cos_angle - z * sin_angle + 0.6
            v = x * sin_angle + z * cos_angle + 0.9
            course = math.floor(v / 2.8)
            u += 1.8 if course % 2 else 0
            slab = math.floor(u / 3.6)
            across, along = u % 3.6, v % 2.8
            distance = min(across, 3.6 - across, along, 2.8 - along)
            joint = math.exp(-(distance / joint_width) ** 2)
            tone = 0.93 + 0.035 * math.sin(slab * 2.4 + course * 1.7)
            grain = 0.009 * math.sin(16 * x + 9 * z) * math.sin(7 * x - 18 * z)
            luminance = (tone + grain) * (1 - 0.22 * joint) * (1 - shade)
            srgb = 1.055 * luminance ** (1 / 2.4) - 0.055
            channel = round(srgb * 255)
            pixels.extend((channel, channel, channel))
    header = struct.pack(">2I5B", SIZE, SIZE, 8, 2, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(pixels, 9))
        + png_chunk(b"IEND", b"")
    )


def generate(output):
    edge = HALF_FLOOR
    positions = [-edge, 0, -edge, -edge, 0, edge, edge, 0, edge, edge, 0, -edge]
    geometry = struct.pack("<24f6H", *positions, *([0, 1, 0] * 4), 0, 1, 2, 0, 2, 3)
    uv = struct.pack("<8f", 0, 0, 0, 1, 1, 1, 1, 0)
    texture = paving_texture()
    data = geometry + uv + texture
    gltf = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [{"primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 3},
            "indices": 2,
            "material": 0,
        }]}],
        "materials": [{"extras": {"novaHybridReflectionStrength": 0.65}, "pbrMetallicRoughness": {
            "baseColorFactor": [0.19, 0.175, 0.15, 1],
            "baseColorTexture": {"index": 0},
            "metallicFactor": 0,
            "roughnessFactor": 0.52,
        }}],
        "textures": [{"source": 0}],
        "images": [{"bufferView": 4, "mimeType": "image/png"}],
        "buffers": [{"byteLength": len(data)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": offset, "byteLength": length}
            for offset, length in [(0, 48), (48, 48), (96, 12), (len(geometry), len(uv)),
                                   (len(geometry) + len(uv), len(texture))]
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3",
             "min": [-edge, 0, -edge], "max": [edge, 0, edge]},
            {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5123, "count": 6, "type": "SCALAR"},
            {"bufferView": 3, "componentType": 5126, "count": 4, "type": "VEC2"},
        ],
    }
    descriptor = json.dumps(gltf, separators=(",", ":")).encode()
    descriptor += b" " * (-len(descriptor) % 4)
    data += b"\0" * (-len(data) % 4)
    output.write_bytes(
        struct.pack("<4sII", b"glTF", 2, 28 + len(descriptor) + len(data))
        + struct.pack("<I4s", len(descriptor), b"JSON") + descriptor
        + struct.pack("<I4s", len(data), b"BIN\0") + data
    )


if __name__ == "__main__":
    generate(Path("assets/CarConcept/Ground.glb"))
