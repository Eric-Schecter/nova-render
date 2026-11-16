// Self-authored two-slot cubes with repeated UVs for material/texture editing checks.
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs'
const original = readFileSync('assets/SceneSample/Red.glb'),
  length = original.readUInt32LE(12),
  doc = JSON.parse(original.subarray(20, 20 + length)),
  bin = original.subarray(28 + length)
const source = new Float32Array(bin.buffer, bin.byteOffset, 108),
  normals = new Float32Array(bin.buffer, bin.byteOffset + 432, 108)
const chunks = [],
  views = [],
  accessors = [],
  primitives = []
let offset = 0
for (let slot = 0; slot < 2; slot++) {
  const positions = new Float32Array(source.length),
    uv = new Float32Array(72)
  for (let i = 0; i < 36; i++) {
    for (let k = 0; k < 3; k++)
      positions[i * 3 + k] = source[i * 3 + k] * 0.9 + (k === 0 ? (slot ? 0.6 : -0.6) : 0)
    const pattern = [
      [0, 0],
      [2, 0],
      [2, 2],
      [0, 0],
      [2, 2],
      [0, 2],
    ][i % 6]
    uv.set(pattern, i * 2)
  }
  const attrs = {}
  for (const [name, array, size] of [
    ['POSITION', positions, 3],
    ['NORMAL', normals, 3],
    ['TEXCOORD_0', uv, 2],
  ]) {
    attrs[name] = accessors.length
    accessors.push({
      bufferView: views.length,
      componentType: 5126,
      count: 36,
      type: 'VEC' + size,
      ...(name === 'POSITION'
        ? { min: [slot ? 0.15 : -1.05, -0.45, -0.45], max: [slot ? 1.05 : -0.15, 0.45, 0.45] }
        : {}),
    })
    views.push({ buffer: 0, byteOffset: offset, byteLength: array.byteLength })
    chunks.push(Buffer.from(array.buffer, array.byteOffset, array.byteLength))
    offset += array.byteLength
  }
  primitives.push({ attributes: attrs, material: slot })
}
const payload = Buffer.concat(chunks),
  json = {
    asset: { version: '2.0', generator: 'Nova Render material sample' },
    scene: 0,
    scenes: [{ nodes: [0] }],
    nodes: [{ mesh: 0 }],
    meshes: [{ primitives }],
    materials: [
      {
        name: 'Left',
        pbrMetallicRoughness: {
          baseColorFactor: [1, 0.03, 0.02, 1],
          metallicFactor: 0,
          roughnessFactor: 0.65,
        },
      },
      {
        name: 'Right',
        pbrMetallicRoughness: {
          baseColorFactor: [0.02, 0.08, 1, 1],
          metallicFactor: 0,
          roughnessFactor: 0.65,
        },
      },
    ],
    buffers: [{ byteLength: payload.length }],
    bufferViews: views,
    accessors,
  }
const raw = Buffer.from(JSON.stringify(json)),
  text = Buffer.alloc(Math.ceil(raw.length / 4) * 4, 32)
raw.copy(text)
const header = Buffer.alloc(20),
  bh = Buffer.alloc(8)
header.writeUInt32LE(0x46546c67)
header.writeUInt32LE(2, 4)
header.writeUInt32LE(28 + text.length + payload.length, 8)
header.writeUInt32LE(text.length, 12)
header.writeUInt32LE(0x4e4f534a, 16)
bh.writeUInt32LE(payload.length)
bh.writeUInt32LE(0x004e4942, 4)
mkdirSync('assets/MaterialSample', { recursive: true })
writeFileSync('assets/MaterialSample/Dual.glb', Buffer.concat([header, text, bh, payload]))
