// Self-contained Cornell-style scene, authored for Nova. No downloaded geometry.
import { deflateSync } from 'node:zlib'
import { writeFile, mkdir } from 'node:fs/promises'
const chunks = [],
  views = [],
  accessors = [],
  positions = [],
  normals = [],
  uvs = [],
  indices = []
const palette = [
  [210, 210, 210, 255],
  [190, 32, 24, 255],
  [30, 160, 55, 255],
  [235, 235, 235, 255],
]
function crc(bytes) {
  let n = 0xffffffff
  for (const b of bytes) {
    n ^= b
    for (let i = 0; i < 8; i++) n = (n >>> 1) ^ (n & 1 ? 0xedb88320 : 0)
  }
  return (n ^ 0xffffffff) >>> 0
}
function chunk(name, bytes) {
  const type = Buffer.from(name),
    size = Buffer.alloc(4),
    sum = Buffer.alloc(4)
  size.writeUInt32BE(bytes.length)
  sum.writeUInt32BE(crc(Buffer.concat([type, bytes])))
  return Buffer.concat([size, type, bytes, sum])
}
function png(emission) {
  const header = Buffer.alloc(13)
  header.writeUInt32BE(32)
  header.writeUInt32BE(8, 4)
  header[8] = 8
  header[9] = 6
  const rows = []
  for (let y = 0; y < 8; y++) {
    const row = [0]
    for (let x = 0; x < 32; x++) {
      const k = x >> 3
      row.push(...(emission ? (k === 3 ? [255, 255, 255, 255] : [0, 0, 0, 255]) : palette[k]))
    }
    rows.push(Buffer.from(row))
  }
  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', header),
    chunk('IDAT', deflateSync(Buffer.concat(rows))),
    chunk('IEND', Buffer.alloc(0)),
  ])
}
function quad(points, normal, color = 0) {
  const start = positions.length / 3
  for (const p of points) {
    positions.push(...p)
    normals.push(...normal)
    uvs.push((color + 0.5) / 4, 0.5)
  }
  indices.push(start, start + 1, start + 2, start, start + 2, start + 3)
}
quad(
  [
    [-1, -1, 1],
    [1, -1, 1],
    [1, -1, -1],
    [-1, -1, -1],
  ],
  [0, 1, 0],
)
quad(
  [
    [-1, 1, -1],
    [1, 1, -1],
    [1, 1, 1],
    [-1, 1, 1],
  ],
  [0, -1, 0],
)
quad(
  [
    [-1, -1, -1],
    [1, -1, -1],
    [1, 1, -1],
    [-1, 1, -1],
  ],
  [0, 0, 1],
)
quad(
  [
    [-1, -1, 1],
    [-1, -1, -1],
    [-1, 1, -1],
    [-1, 1, 1],
  ],
  [1, 0, 0],
  1,
)
quad(
  [
    [1, -1, -1],
    [1, -1, 1],
    [1, 1, 1],
    [1, 1, -1],
  ],
  [-1, 0, 0],
  2,
)
// Slightly below the ceiling, facing into the room.
quad(
  [
    [-0.35, 0.995, -0.45],
    [0.35, 0.995, -0.45],
    [0.35, 0.995, 0.2],
    [-0.35, 0.995, 0.2],
  ],
  [0, -1, 0],
  3,
)
function box(cx, cz, sx, sz, height, angle) {
  const c = Math.cos(angle),
    s = Math.sin(angle),
    point = ([x, y, z]) => [cx + c * x + s * z, y, cz - s * x + c * z],
    normal = ([x, y, z]) => [c * x + s * z, y, -s * x + c * z]
  const x = sx / 2,
    z = sz / 2,
    b = -1,
    t = -1 + height
  for (const [p, n] of [
    [
      [
        [-x, t, z],
        [x, t, z],
        [x, t, -z],
        [-x, t, -z],
      ],
      [0, 1, 0],
    ],
    [
      [
        [-x, b, z],
        [x, b, z],
        [x, t, z],
        [-x, t, z],
      ],
      [0, 0, 1],
    ],
    [
      [
        [x, b, -z],
        [-x, b, -z],
        [-x, t, -z],
        [x, t, -z],
      ],
      [0, 0, -1],
    ],
    [
      [
        [-x, b, -z],
        [-x, b, z],
        [-x, t, z],
        [-x, t, -z],
      ],
      [-1, 0, 0],
    ],
    [
      [
        [x, b, z],
        [x, b, -z],
        [x, t, -z],
        [x, t, z],
      ],
      [1, 0, 0],
    ],
  ])
    quad(p.map(point), normal(n))
}
box(-0.4, 0.35, 0.58, 0.6, 0.58, -0.25)
box(0.35, -0.27, 0.6, 0.6, 1.15, 0.3)
function view(bytes) {
  const size = chunks.reduce((n, b) => n + b.length, 0),
    padding = Buffer.alloc((4 - (size % 4)) % 4)
  chunks.push(padding)
  const offset = size + padding.length
  chunks.push(bytes)
  views.push({ buffer: 0, byteOffset: offset, byteLength: bytes.length })
  return views.length - 1
}
function accessor(values, type, components, componentType = 5126) {
  const bytes =
      componentType === 5126
        ? Buffer.from(new Float32Array(values).buffer)
        : Buffer.from(new Uint16Array(values).buffer),
    bufferView = view(bytes),
    item = { bufferView, componentType, count: values.length / components, type }
  if (accessors.length === 0) {
    item.min = [-1, -1, -1]
    item.max = [1, 1, 1]
  }
  accessors.push(item)
  return accessors.length - 1
}
const position = accessor(positions, 'VEC3', 3),
  normal = accessor(normals, 'VEC3', 3),
  uv = accessor(uvs, 'VEC2', 2),
  index = accessor(indices, 'SCALAR', 1, 5123),
  base = view(png(false)),
  emission = view(png(true))
let bin = Buffer.concat(chunks)
bin = Buffer.concat([bin, Buffer.alloc((4 - (bin.length % 4)) % 4)])
const gltf = {
  asset: { version: '2.0', generator: 'Nova Cornell-style scene generator' },
  scene: 0,
  scenes: [{ nodes: [0] }],
  nodes: [{ mesh: 0 }],
  meshes: [
    {
      primitives: [
        {
          attributes: { POSITION: position, NORMAL: normal, TEXCOORD_0: uv },
          indices: index,
          material: 0,
        },
      ],
    },
  ],
  materials: [
    {
      name: 'Cornell atlas',
      doubleSided: true,
      pbrMetallicRoughness: {
        baseColorTexture: { index: 0 },
        metallicFactor: 0,
        roughnessFactor: 1,
      },
      emissiveTexture: { index: 1 },
      emissiveFactor: [1, 1, 1],
    },
  ],
  textures: [{ source: 0 }, { source: 1 }],
  images: [
    { bufferView: base, mimeType: 'image/png' },
    { bufferView: emission, mimeType: 'image/png' },
  ],
  buffers: [{ byteLength: bin.length }],
  bufferViews: views,
  accessors,
}
let json = Buffer.from(JSON.stringify(gltf))
json = Buffer.concat([json, Buffer.alloc((4 - (json.length % 4)) % 4, 32)])
const header = Buffer.alloc(12),
  j = Buffer.alloc(8),
  b = Buffer.alloc(8)
header.writeUInt32LE(0x46546c67)
header.writeUInt32LE(2, 4)
header.writeUInt32LE(28 + json.length + bin.length, 8)
j.writeUInt32LE(json.length)
j.writeUInt32LE(0x4e4f534a, 4)
b.writeUInt32LE(bin.length)
b.writeUInt32LE(0x004e4942, 4)
await mkdir('assets/CornellBox', { recursive: true })
await writeFile('assets/CornellBox/CornellBox.glb', Buffer.concat([header, j, json, b, bin]))
console.log('CornellBox:', indices.length / 3, 'triangles')
