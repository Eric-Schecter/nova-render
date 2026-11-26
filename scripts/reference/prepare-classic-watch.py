#!/usr/bin/env python3
"""Map Classic Watch materials onto a local, already decompressed Nova GLB.

Usage: prepare-classic-watch.py original.glb nova-baseline.glb leather.jpg output.glb
Requires ffmpeg and ffprobe. The baseline must preserve the watch materials and
must already be free of Draco and unsupported scene extensions.
"""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile


def read_glb(path):
    data = path.read_bytes()
    magic, version, length, size, kind = struct.unpack_from("<4sIII4s", data)
    if (magic, version, length, kind) != (b"glTF", 2, len(data), b"JSON"):
        raise ValueError(f"Expected a glTF 2 GLB: {path}")
    bin_size, bin_kind = struct.unpack_from("<I4s", data, 20 + size)
    if bin_kind != b"BIN\0" or 28 + size + bin_size != length:
        raise ValueError(f"Expected a single BIN chunk: {path}")
    return json.loads(data[20:20 + size]), bytearray(data[28 + size:])


def texture_bytes(doc, binary, index):
    texture = doc["textures"][index]
    image = doc["images"][texture["source"]]
    view = doc["bufferViews"][image["bufferView"]]
    start = view.get("byteOffset", 0)
    return binary[start:start + view["byteLength"]]


def ffmpeg(*args, pixels=None):
    return subprocess.run(
        ["ffmpeg", "-v", "error", *args], input=pixels,
        stdout=subprocess.PIPE, check=True,
    ).stdout


def decode(path, flip=False):
    dimensions = subprocess.check_output([
        "ffprobe", "-v", "error", "-select_streams", "v:0",
        "-show_entries", "stream=width,height", "-of", "csv=p=0", str(path),
    ], text=True).strip()
    filters = ["-vf", "vflip"] if flip else []
    pixels = ffmpeg("-i", str(path), *filters, "-frames:v", "1", "-pix_fmt",
                    "rgb24", "-f", "rawvideo", "pipe:1")
    return dimensions.replace(",", "x"), bytearray(pixels)


def append_texture(doc, binary, name, size, pixels):
    png = ffmpeg("-f", "rawvideo", "-pixel_format", "rgb24", "-video_size", size,
                 "-i", "pipe:0", "-frames:v", "1", "-c:v", "png",
                 "-f", "image2pipe", "pipe:1", pixels=pixels)
    binary.extend(bytes(-len(binary) % 4))
    views, images, textures = (doc.setdefault(key, []) for key in ("bufferViews", "images", "textures"))
    views.append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(png)})
    images.append({"name": name, "mimeType": "image/png", "bufferView": len(views) - 1})
    textures.append({"source": len(images) - 1})
    binary.extend(png)
    return len(textures) - 1


def convert(original_path, baseline_path, leather_path, output):
    if output.resolve() in {original_path.resolve(), baseline_path.resolve(), leather_path.resolve()}:
        raise ValueError("Choose a separate output GLB")
    original, original_binary = read_glb(original_path)
    doc, binary = read_glb(baseline_path)
    originals = {material["name"]: material for material in original["materials"]}
    materials = {material["name"]: material for material in doc["materials"]}
    strap = originals["strap"]["extensions"]["WEBGI_material_extras"]
    resources = original["scenes"][original.get("scene", 0)]["extensions"]["WEBGI_material_extras"]["resources"]
    texture = resources["textures"][strap["normalMap"]["uuid"]]
    normal_x, normal_y = strap["normalScale"]
    if normal_x <= 0 or abs(normal_x) != abs(normal_y):
        raise ValueError("Expected matching normal-map axis magnitudes")
    size, pixels = decode(leather_path, flip=texture["flipY"])
    if normal_y < 0:
        pixels[1::3] = bytes(255 - value for value in pixels[1::3])
    index = append_texture(doc, binary, "Leather normal", size, pixels)
    materials["strap"]["normalTexture"] = {
        "index": index, "scale": normal_x,
        "extensions": {"KHR_texture_transform": {"scale": texture["repeat"]}},
    }

    with tempfile.TemporaryDirectory() as scratch:
        map_path = Path(scratch) / "direction"
        for name in ("metal_cover", "clockface"):
            old = originals[name]["extensions"]["WEBGI_materials_anisotropy"]
            map_path.write_bytes(texture_bytes(original, original_binary, old["anisotropyDirection"]["index"]))
            size, pixels = decode(map_path)
            mode = old["anisotropyDirectionMode"]
            if mode == "ROTATION":
                # WebGi encodes one turn in red and builds its axis as (sin, cos).
                for i in range(0, len(pixels), 3):
                    angle = math.tau * pixels[i] / 255
                    pixels[i] = round(127.5 * (1 + math.sin(angle)))
                    pixels[i + 1] = round(127.5 * (1 + math.cos(angle)))
            elif mode != "DIRECTION":
                raise ValueError(f"Unknown direction encoding: {mode}")
            # WebGi uses a material-wide factor. glTF multiplies it by blue.
            pixels[2::3] = bytes([255]) * (len(pixels) // 3)
            index = append_texture(doc, binary, f"{name} anisotropy direction", size, pixels)
            # The legacy dial factor exceeds glTF's range. A lower strength
            # retains the brushed direction without the harsh radial lobes.
            strength = .45 if name == "clockface" else min(1, max(0, old["anisotropyFactor"]))
            materials[name].setdefault("extensions", {})["KHR_materials_anisotropy"] = {
                "anisotropyStrength": strength,
                "anisotropyTexture": {"index": index},
            }

    for extension in ("KHR_texture_transform", "KHR_materials_anisotropy"):
        if extension not in doc.setdefault("extensionsUsed", []):
            doc["extensionsUsed"].append(extension)
    doc["buffers"][0]["byteLength"] = len(binary)
    binary.extend(bytes(-len(binary) % 4))
    header = json.dumps(doc, separators=(",", ":")).encode()
    header += b" " * (-len(header) % 4)
    with output.open("wb") as stream:
        stream.write(struct.pack("<4sIII4s", b"glTF", 2, 28 + len(header) + len(binary), len(header), b"JSON"))
        stream.write(header)
        stream.write(struct.pack("<I4s", len(binary), b"BIN\0"))
        stream.write(binary)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ("original", "baseline", "leather", "output"):
        parser.add_argument(arg, type=Path)
    args = parser.parse_args()
    convert(args.original, args.baseline, args.leather, args.output)
