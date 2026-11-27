"""Bake a soft directional ground mask for the static WebGi engine fixture with Blender."""

import argparse
import importlib.util
import math
from pathlib import Path
import struct
import sys
import zlib

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

spec = importlib.util.spec_from_file_location(
    "engine_fixture", Path(__file__).with_name("prepare-webgi-engine.py")
)
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


def png_chunk(tag, payload):
    return struct.pack(">I", len(payload)) + tag + payload + struct.pack(
        ">I", zlib.crc32(tag + payload)
    )


def append_view(scene, data, payload):
    index = len(scene["bufferViews"])
    scene["bufferViews"].append({"buffer": 0, "byteOffset": len(data), "byteLength": len(payload)})
    data.extend(payload)
    data.extend(b"\0" * (-len(data) % 4))
    return index


def bake(engine, floor, output, light):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(engine.resolve()))
    vertices, faces = [], []
    for mesh in bpy.context.scene.objects:
        if mesh.type != "MESH":
            continue
        start = len(vertices)
        vertices.extend(mesh.matrix_world @ vertex.co for vertex in mesh.data.vertices)
        faces.extend(tuple(start + index for index in face.vertices) for face in mesh.data.polygons)
    if not faces:
        raise ValueError("Engine has no shadow-casting geometry")
    bvh = BVHTree.FromPolygons(vertices, faces)

    scene, data = fixture.read_glb(floor)
    primitive = scene["meshes"][0]["primitives"][0]
    position = scene["accessors"][primitive["attributes"]["POSITION"]]
    view = scene["bufferViews"][position["bufferView"]]
    offset = view.get("byteOffset", 0) + position.get("byteOffset", 0)
    points = [struct.unpack_from("<3f", data, offset + i * 12) for i in range(position["count"])]
    xmin, height, zmin = position["min"]
    xmax, _, zmax = position["max"]
    if any(abs(y - height) > 1e-5 for _, y, _ in points):
        raise ValueError("Expected a horizontal fixture floor")
    uv = b"".join(
        struct.pack("<2f", (x - xmin) / (xmax - xmin), (z - zmin) / (zmax - zmin))
        for x, _, z in points
    )
    primitive["attributes"]["TEXCOORD_0"] = len(scene["accessors"])
    scene["accessors"].append({
        "bufferView": append_view(scene, data, uv),
        "componentType": 5126, "count": len(points), "type": "VEC2",
    })

    # glTF (x, y, z) becomes Blender (x, -z, y). Sample a disk around the light direction.
    center = Vector((light[0], -light[2], light[1])).normalized()
    tangent = center.cross(Vector((0, 1, 0))).normalized()
    bitangent = center.cross(tangent)
    directions = []
    for i in range(48):
        radius = .30 * math.sqrt((i + .5) / 48)
        angle = i * 2.39996323
        directions.append((center + radius * (math.cos(angle) * tangent + math.sin(angle) * bitangent)).normalized())

    size = 192
    pixels = bytearray()
    for row in range(size):
        pixels.append(0)  # PNG scanline filter.
        z = zmin + (zmax - zmin) * (row + .5) / size
        for col in range(size):
            x = xmin + (xmax - xmin) * (col + .5) / size
            origin = Vector((x, -z, height + .005))
            hits = sum(bvh.ray_cast(origin, direction, 8)[0] is not None for direction in directions)
            pixels.append(round(255 * (1 - hits / len(directions))))
    png = b"\x89PNG\r\n\x1a\n" + png_chunk(
        b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 0, 0, 0, 0)
    ) + png_chunk(b"IDAT", zlib.compress(pixels)) + png_chunk(b"IEND", b"")
    scene["images"] = [{"bufferView": append_view(scene, data, png), "mimeType": "image/png"}]
    scene["textures"] = [{"source": 0}]
    scene["materials"][0]["occlusionTexture"] = {"index": 0, "strength": .48}
    fixture.write_glb(output, scene, data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path, help="Decoded engine GLB in fixture world space")
    parser.add_argument("floor", type=Path, help="Ground GLB produced by prepare-webgi-engine.py")
    parser.add_argument("output", type=Path, help="Ground GLB with an embedded occlusion map")
    parser.add_argument("--light", type=float, nargs=3, default=(.12, .92, .38),
                        help="glTF direction toward the center of the shadow-casting light")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1 :])
    if not all(math.isfinite(component) for component in args.light) or args.light[1] <= 0:
        parser.error("--light must be a finite direction above the ground")
    bake(args.engine, args.floor, args.output, args.light)


if __name__ == "__main__":
    main()
