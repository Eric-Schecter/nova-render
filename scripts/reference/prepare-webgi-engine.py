"""Prepare local Nova comparison assets from a decoded WebGi engine and its textures."""

import argparse
import json
from pathlib import Path
import struct
import subprocess


def read_glb(path):
    data = path.read_bytes()
    magic, version, length, json_length, chunk = struct.unpack_from("<4sIIII", data)
    if (magic, version, length, chunk) != (b"glTF", 2, len(data), 0x4E4F534A):
        raise ValueError(f"Expected a glTF 2.0 binary: {path}")
    end = 20 + json_length
    binary_length, binary_type = struct.unpack_from("<II", data, end)
    if binary_type != 0x004E4942 or end + 8 + binary_length != len(data):
        raise ValueError(f"Expected a single BIN chunk: {path}")
    return json.loads(data[20:end]), bytearray(data[end + 8 :])


def write_glb(path, scene, data):
    scene["buffers"][0]["byteLength"] = len(data)
    metadata = json.dumps(scene, separators=(",", ":")).encode()
    metadata += b" " * (-len(metadata) % 4)
    chunks = (
        struct.pack("<II", len(metadata), 0x4E4F534A)
        + metadata
        + struct.pack("<II", len(data), 0x004E4942)
        + data
    )
    path.write_bytes(struct.pack("<4sII", b"glTF", 2, 12 + len(chunks)) + chunks)


def texture(scene, data, path, channel_filter):
    png = subprocess.check_output(
        [
            "ffmpeg", "-v", "error", "-i", str(path), "-frames:v", "1",
            "-vf", f"format=rgb24,{channel_filter}", "-c:v", "png", "-f", "image2pipe", "-",
        ],
    )
    view = len(scene["bufferViews"])
    scene["bufferViews"].append({"buffer": 0, "byteOffset": len(data), "byteLength": len(png)})
    data.extend(png)
    data.extend(b"\0" * (-len(data) % 4))
    images = scene.setdefault("images", [])
    images.append({"bufferView": view, "mimeType": "image/png"})
    textures = scene.setdefault("textures", [])
    textures.append({"source": len(images) - 1})
    return len(textures) - 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path, help="Draco-decoded engine GLB compatible with Nova")
    parser.add_argument("roughness", type=Path, help="Aluminum roughness texture")
    parser.add_argument("normal", type=Path, help="Aluminum normal texture")
    parser.add_argument("floor", type=Path, help="Original Nova comparison floor GLB")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    scene, data = read_glb(args.engine)
    fan = next(material for material in scene["materials"] if material["name"] == "lambert4")
    fan.setdefault("extras", {})["novaTriplanar"] = True
    pbr = fan["pbrMetallicRoughness"]
    pbr["roughnessFactor"] = 1
    pbr["metallicRoughnessTexture"] = {
        "index": texture(scene, data, args.roughness, "lutrgb=r=255:b=255"),
    }
    # WebGi uses normalScale [2, -2]. Flip green to retain the sign with glTF's scalar scale.
    fan["normalTexture"] = {
        "index": texture(scene, data, args.normal, "lutrgb=g=negval"),
        "scale": 2,
    }
    write_glb(args.output / "engine-ssr-compressed-triplanar.glb", scene, data)

    for name, color, reflection_strength in [
        ("dark", 0.060, None),
        ("dark-reflective", 0.060, 0.80),
        ("dark-full-trace", 0.060, 1),
    ]:
        scene, data = read_glb(args.floor)
        ground = scene["materials"][0]
        ground["pbrMetallicRoughness"]["baseColorFactor"] = [color, color, color, 1]
        if reflection_strength is not None:
            ground.setdefault("extras", {})["novaHybridReflectionStrength"] = reflection_strength
        write_glb(args.output / f"engine-ssr-compressed-{name}.glb", scene, data)

    # Use the shared Hybrid AO passes for contact shading under the HDRI.
    ao = {
        "ambientOcclusion": True,
        "aoSamples": 16,
        "halfResolutionAO": True,
        "blueNoiseAO": True,
        "separateSpecularOcclusion": True,
    }
    (args.output / "engine-hybrid-ao.json").write_text(json.dumps(ao, indent=2) + "\n")

    display = {
        "clearColor": [77 / 255, 175 / 255, 1, 1],
        "post": {"toneMapBackground": False},
    }
    (args.output / "engine-display.json").write_text(json.dumps(display, indent=2) + "\n")


if __name__ == "__main__":
    main()
