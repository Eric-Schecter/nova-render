#!/usr/bin/env python3
"""Write the Classic Watch reference stage settings and a procedural floor GLB."""
import argparse
import json
from pathlib import Path
import struct


def make_floor(path):
    # Source-space watch bounds put the bottom of the strap at y=-1.502.
    edge, y = 48, -1.502
    positions = [-edge, y, -edge, -edge, y, edge, edge, y, edge, edge, y, -edge]
    data = struct.pack("<24f6H", *positions, *([0, 1, 0] * 4), 0, 1, 2, 0, 2, 3)
    scene = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2, "material": 0}]}],
        "extensionsUsed": ["KHR_materials_specular"],
        # Keep a small floor-only radiance offset below the filmic black threshold.
        # It exposes partial visibility without lifting the fully occluded contact.
        "materials": [{"emissiveFactor": [.052, .053, .056],
                       "extensions": {"KHR_materials_specular": {"specularFactor": .04}},
                       "pbrMetallicRoughness": {
            "baseColorFactor": [.0032, .0032, .0036, 1], "roughnessFactor": .48, "metallicFactor": 0,
        }}],
        "buffers": [{"byteLength": len(data)}],
        "bufferViews": [{"buffer": 0, "byteOffset": offset, "byteLength": length}
                        for offset, length in ((0, 48), (48, 48), (96, 12))],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3",
             "min": [-edge, y, -edge], "max": [edge, y, edge]},
            {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5123, "count": 6, "type": "SCALAR"},
        ],
    }
    descriptor = json.dumps(scene, separators=(",", ":")).encode()
    descriptor += b" " * (-len(descriptor) % 4)
    path.write_bytes(struct.pack("<4sIII4s", b"glTF", 2, 28 + len(descriptor) + len(data),
                                 len(descriptor), b"JSON") + descriptor +
                     struct.pack("<I4s", len(data), b"BIN\0") + data)


def make_stage(path):
    stage = {
        "clearColor": [.308, .311, .322, 1],
        "environment": {"rotation": -133.84, "intensity": .770914, "showBackground": False, "backgroundIntensity": 1},
        "light": {
            "id": "watch-key", "type": "directional", "color": [1, 1, 1],
            "direction": [-.3296544, .8968639, .2948951], "intensity": 16,
            "castShadow": True, "shadowSoftness": 12,
        },
        "shadows": {"enabled": True, "samples": 16, "bias": .002},
        "post": {
            "enabled": True, "toneMapping": "aces-filmic", "exposureEV": 0,
            "contrast": 1.1, "bloom": True, "bloomThreshold": 1.82,
            "bloomKnee": .67, "bloomIntensity": .39,
        },
    }
    path.write_text(json.dumps(stage, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    make_floor(args.output / "watch-floor.glb")
    make_stage(args.output / "watch-stage.json")
