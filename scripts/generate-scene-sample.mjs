// Tiny self-authored static GLBs: non-centered, authored node scale and two materials.
import { writeFileSync, mkdirSync } from 'node:fs'
mkdirSync('assets/SceneSample', { recursive: true })
const faces = [
  [
    [-1, -1, 1],
    [1, -1, 1],
    [1, 1, 1],
    [-1, 1, 1],
  ],
  [
    [1, -1, -1],
    [-1, -1, -1],
    [-1, 1, -1],
    [1, 1, -1],
  ],
  [
    [1, -1, 1],
    [1, -1, -1],
    [1, 1, -1],
    [1, 1, 1],
  ],
  [
    [-1, -1, -1],
    [-1, -1, 1],
    [-1, 1, 1],
    [-1, 1, -1],
  ],
  [
    [-1, 1, 1],
    [1, 1, 1],
    [1, 1, -1],
    [-1, 1, -1],
  ],
  [
    [-1, -1, -1],
    [1, -1, -1],
    [1, -1, 1],
    [-1, -1, 1],
  ],
]
const normals = [
  [0, 0, 1],
  [0, 0, -1],
  [1, 0, 0],
  [-1, 0, 0],
  [0, 1, 0],
  [0, -1, 0],
]
for (const [name, color] of [
  ['Red', [1, 0.04, 0.02, 1]],
  ['Blue', [0.02, 0.15, 1, 1]],
]) {
  const positions = [],
    ns = []
  faces.forEach((face, f) => {
    for (const i of [0, 1, 2, 0, 2, 3]) {
      positions.push(...face[i].map((v) => v * 0.5))
      ns.push(...normals[f])
    }
  })
  const bin = Buffer.concat([
    Buffer.from(new Float32Array(positions).buffer),
    Buffer.from(new Float32Array(ns).buffer),
  ])
  const json = {
    asset: { version: '2.0', generator: 'Nova Render scene sample' },
    scene: 0,
    scenes: [{ nodes: [0] }],
    nodes: [{ mesh: 0, translation: [2, 1, 0], scale: [4, 2, 2] }],
    meshes: [{ primitives: [{ attributes: { POSITION: 0, NORMAL: 1 }, material: 0 }] }],
    materials: [
      { pbrMetallicRoughness: { baseColorFactor: color, metallicFactor: 0, roughnessFactor: 0.6 } },
    ],
    buffers: [{ byteLength: bin.length }],
    bufferViews: [
      { buffer: 0, byteOffset: 0, byteLength: positions.length * 4 },
      { buffer: 0, byteOffset: positions.length * 4, byteLength: ns.length * 4 },
    ],
    accessors: [
      {
        bufferView: 0,
        componentType: 5126,
        count: 36,
        type: 'VEC3',
        min: [-0.5, -0.5, -0.5],
        max: [0.5, 0.5, 0.5],
      },
      { bufferView: 1, componentType: 5126, count: 36, type: 'VEC3' },
    ],
  }
  const raw = Buffer.from(JSON.stringify(json)),
    doc = Buffer.alloc(Math.ceil(raw.length / 4) * 4, 32)
  raw.copy(doc)
  const header = Buffer.alloc(20)
  header.writeUInt32LE(0x46546c67)
  header.writeUInt32LE(2, 4)
  header.writeUInt32LE(28 + doc.length + bin.length, 8)
  header.writeUInt32LE(doc.length, 12)
  header.writeUInt32LE(0x4e4f534a, 16)
  const chunk = Buffer.alloc(8)
  chunk.writeUInt32LE(bin.length)
  chunk.writeUInt32LE(0x004e4942, 4)
  writeFileSync(`assets/SceneSample/${name}.glb`, Buffer.concat([header, doc, chunk, bin]))
}
