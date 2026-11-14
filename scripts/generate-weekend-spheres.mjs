// Original procedural geometry inspired by the final random-spheres scene in RTIOW.
import { deflateSync } from 'node:zlib'
import { mkdir, writeFile } from 'node:fs/promises'
let seed = 20261114
const random = () => (seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0) / 4294967296
const colors = [
  [128, 128, 128],
  [100, 55, 28],
  [180, 155, 128],
  [245, 245, 245],
  [255, 255, 255],
]
const roughness = [255, 255, 20, 5, 255],
  metallic = [0, 0, 255, 0, 0]
const spheres = [
  [[-3, 1, 0], 1, 1],
  [[3, 1, 0], 1, 2],
  [[0, 1, 0], 1, 3],
]
for (let z = -6; z < 6; z++)
  for (let x = -6; x < 6; x++) {
    const center = [x + 0.8 * random(), 0.2, z + 0.8 * random()]
    if (spheres.slice(0, 3).some(([c]) => Math.hypot(center[0] - c[0], center[2] - c[2]) < 1.25))
      continue
    const material = random()
    if (material < 0.75) {
      colors.push([0, 0, 0].map(() => Math.round(255 * random() * random())))
      roughness.push(255)
      metallic.push(0)
      spheres.push([center, 0.2, colors.length - 1])
    } else if (material < 0.93) {
      colors.push([0, 0, 0].map(() => Math.round(128 + 127 * random())))
      roughness.push(Math.round(10 + 100 * random()))
      metallic.push(255)
      spheres.push([center, 0.2, colors.length - 1])
    } else spheres.push([center, 0.2, 3])
  }
const positions = [],
  normals = [],
  uvs = [],
  opaque = [],
  glass = []
function triangle(ps, ns, color) {
  const start = positions.length / 3
  for (let i = 0; i < 3; i++) {
    positions.push(...ps[i])
    normals.push(...ns[i])
    uvs.push((color + 0.5) / colors.length, 0.5)
  }
  ;(color === 3 ? glass : opaque).push(start, start + 1, start + 2)
}
for (const [center, radius, color] of spheres) {
  const rows = radius === 1 ? 24 : 10,
    columns = rows * 2
  const normal = (y, x) => [
    Math.sin((y * Math.PI) / rows) * Math.cos((x * 2 * Math.PI) / columns),
    Math.cos((y * Math.PI) / rows),
    Math.sin((y * Math.PI) / rows) * Math.sin((x * 2 * Math.PI) / columns),
  ]
  for (let y = 0; y < rows; y++)
    for (let x = 0; x < columns; x++) {
      const a = normal(y, x),
        b = normal(y + 1, x),
        c = normal(y + 1, x + 1),
        d = normal(y, x + 1)
      for (const ns of [y < rows - 1 ? [a, c, b] : null, y > 0 ? [a, d, c] : null])
        if (ns)
          triangle(
            ns.map((n) => n.map((v, k) => center[k] + radius * v)),
            ns,
            color,
          )
    }
}
function quad(ps, n, color) {
  triangle([ps[0], ps[1], ps[2]], [n, n, n], color)
  triangle([ps[0], ps[2], ps[3]], [n, n, n], color)
}
quad(
  [
    [-8, 0, 8],
    [8, 0, 8],
    [8, 0, -8],
    [-8, 0, -8],
  ],
  [0, 1, 0],
  0,
)
// Finite light permits the same scene to work in emitter-only integrator comparison.
quad(
  [
    [-3, 5, -3],
    [3, 5, -3],
    [3, 5, 3],
    [-3, 5, 3],
  ],
  [0, -1, 0],
  4,
)
function crc(bytes) {
  let n = 0xffffffff
  for (const b of bytes) {
    n ^= b
    for (let i = 0; i < 8; i++) n = (n >>> 1) ^ (n & 1 ? 0xedb88320 : 0)
  }
  return (n ^ 0xffffffff) >>> 0
}
function chunk(name, bytes) {
  const t = Buffer.from(name),
    h = Buffer.alloc(4),
    c = Buffer.alloc(4)
  h.writeUInt32BE(bytes.length)
  c.writeUInt32BE(crc(Buffer.concat([t, bytes])))
  return Buffer.concat([h, t, bytes, c])
}
function png(mode) {
  const h = Buffer.alloc(13)
  h.writeUInt32BE(colors.length)
  h.writeUInt32BE(1, 4)
  h[8] = 8
  h[9] = 6
  const row = [0]
  colors.forEach((c, i) =>
    row.push(
      ...(mode === 0
        ? [...c, 255]
        : mode === 1
          ? [255, roughness[i], metallic[i], 255]
          : i === 4
            ? [255, 255, 255, 255]
            : [0, 0, 0, 255]),
    ),
  )
  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', h),
    chunk('IDAT', deflateSync(Buffer.from(row))),
    chunk('IEND', Buffer.alloc(0)),
  ])
}
const chunks = [],
  bufferViews = [],
  accessors = []
function view(bytes) {
  const size = chunks.reduce((n, b) => n + b.length, 0),
    pad = Buffer.alloc((4 - (size % 4)) % 4)
  chunks.push(pad, bytes)
  bufferViews.push({ buffer: 0, byteOffset: size + pad.length, byteLength: bytes.length })
  return bufferViews.length - 1
}
function accessor(values, type, components, integer = false) {
  const a = {
    bufferView: view(
      Buffer.from((integer ? new Uint32Array(values) : new Float32Array(values)).buffer),
    ),
    componentType: integer ? 5125 : 5126,
    count: values.length / components,
    type,
  }
  if (type === 'VEC3' && accessors.length === 0) {
    a.min = [-8, 0, -8]
    a.max = [8, 5, 8]
  }
  accessors.push(a)
  return accessors.length - 1
}
const attributes = {
  POSITION: accessor(positions, 'VEC3', 3),
  NORMAL: accessor(normals, 'VEC3', 3),
  TEXCOORD_0: accessor(uvs, 'VEC2', 2),
}
const primitives = [
  { attributes, indices: accessor(opaque, 'SCALAR', 1, true), material: 0 },
  { attributes, indices: accessor(glass, 'SCALAR', 1, true), material: 1 },
]
const images = [0, 1, 2].map((mode) => ({ bufferView: view(png(mode)), mimeType: 'image/png' }))
const base = {
  doubleSided: true,
  pbrMetallicRoughness: {
    baseColorTexture: { index: 0 },
    metallicRoughnessTexture: { index: 1 },
    metallicFactor: 1,
    roughnessFactor: 1,
  },
  emissiveTexture: { index: 2 },
  emissiveFactor: [3, 3, 3],
}
let bin = Buffer.concat(chunks)
bin = Buffer.concat([bin, Buffer.alloc((4 - (bin.length % 4)) % 4)])
const json = {
  asset: { version: '2.0', generator: 'Nova deterministic Weekend Spheres' },
  scene: 0,
  scenes: [{ nodes: [0] }],
  nodes: [{ mesh: 0 }],
  meshes: [{ primitives }],
  materials: [
    { ...base, name: 'Sphere atlas' },
    {
      ...base,
      name: 'Glass',
      extensions: {
        KHR_materials_transmission: { transmissionFactor: 1 },
        KHR_materials_ior: { ior: 1.5 },
      },
    },
  ],
  extensionsUsed: ['KHR_materials_transmission', 'KHR_materials_ior'],
  extensionsRequired: ['KHR_materials_transmission', 'KHR_materials_ior'],
  images,
  textures: images.map((_, source) => ({ source })),
  buffers: [{ byteLength: bin.length }],
  bufferViews,
  accessors,
}
let j = Buffer.from(JSON.stringify(json))
j = Buffer.concat([j, Buffer.alloc((4 - (j.length % 4)) % 4, 32)])
const h = Buffer.alloc(12),
  jh = Buffer.alloc(8),
  bh = Buffer.alloc(8)
h.writeUInt32LE(0x46546c67)
h.writeUInt32LE(2, 4)
h.writeUInt32LE(28 + j.length + bin.length, 8)
jh.writeUInt32LE(j.length)
jh.writeUInt32LE(0x4e4f534a, 4)
bh.writeUInt32LE(bin.length)
bh.writeUInt32LE(0x004e4942, 4)
await mkdir('assets/WeekendSpheres', { recursive: true })
await writeFile('assets/WeekendSpheres/WeekendSpheres.glb', Buffer.concat([h, jh, j, bh, bin]))
console.log(spheres.length, 'spheres,', (opaque.length + glass.length) / 3, 'triangles')
