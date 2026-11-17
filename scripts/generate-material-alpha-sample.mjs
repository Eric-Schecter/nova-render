// Self-authored material conformance fixtures; no external assets.
import { mkdirSync, writeFileSync } from 'node:fs'
import { deflateSync } from 'node:zlib'
const dir = 'assets/MaterialSample'
mkdirSync(dir, { recursive: true })
function crc(bytes) {
  let c = 0xffffffff
  for (const byte of bytes) {
    c ^= byte
    for (let i = 0; i < 8; i++) c = (c >>> 1) ^ (c & 1 ? 0xedb88320 : 0)
  }
  return (c ^ 0xffffffff) >>> 0
}
function pngChunk(type, data) {
  const t = Buffer.from(type),
    n = Buffer.alloc(4),
    c = Buffer.alloc(4)
  n.writeUInt32BE(data.length)
  c.writeUInt32BE(crc(Buffer.concat([t, data])))
  return Buffer.concat([n, t, data, c])
}
const ihdr = Buffer.alloc(13)
ihdr.writeUInt32BE(16)
ihdr.writeUInt32BE(16, 4)
ihdr[8] = 8
ihdr[9] = 6
const scan = Buffer.alloc(16 * 65)
for (let y = 0; y < 16; y++)
  for (let x = 0; x < 16; x++) {
    let i = y * 65 + 1 + x * 4
    scan.set([255, 255, 255, ((x >> 2) + (y >> 2)) % 2 ? 255 : 0], i)
  }
const checker = Buffer.concat([
  Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
  pngChunk('IHDR', ihdr),
  pngChunk('IDAT', deflateSync(scan)),
  pngChunk('IEND', Buffer.alloc(0)),
])
const material = (color, alphaMode = 'OPAQUE', doubleSided = false, texture = false) => ({
  name: alphaMode,
  pbrMetallicRoughness: {
    baseColorFactor: color,
    metallicFactor: 0,
    roughnessFactor: 0.6,
    ...(texture ? { baseColorTexture: { index: 0 } } : {}),
  },
  alphaMode,
  alphaCutoff: 0.5,
  doubleSided,
})
function glb(name, materials, planes, node = {}) {
  const parts = [],
    views = [],
    accessors = []
  let length = 0
  function append(bytes) {
    const pad = Buffer.alloc((4 - (bytes.length % 4)) % 4)
    const index = views.length
    views.push({ buffer: 0, byteOffset: length, byteLength: bytes.length })
    parts.push(bytes, pad)
    length += bytes.length + pad.length
    return index
  }
  function attribute(values, type) {
    const index = accessors.length
    accessors.push({
      bufferView: append(Buffer.from(new Float32Array(values).buffer)),
      componentType: 5126,
      count: values.length / (type === 'VEC2' ? 2 : 3),
      type,
      ...(index % 3 === 0 ? { min: [-10, -10, -10], max: [10, 10, 10] } : {}),
    })
    return index
  }
  const primitives = planes.map(
    ({ x = 0, y = 0, z = 0, w = 1, h = 1, material = 0, back = false }) => {
      const corners = [
          [-w + x, -h + y, z],
          [w + x, -h + y, z],
          [w + x, h + y, z],
          [-w + x, h + y, z],
        ],
        order = back ? [0, 2, 1, 0, 3, 2] : [0, 1, 2, 0, 2, 3],
        uv = [
          [0, 1],
          [1, 1],
          [1, 0],
          [0, 0],
        ]
      return {
        attributes: {
          POSITION: attribute(
            order.flatMap((i) => corners[i]),
            'VEC3',
          ),
          NORMAL: attribute(
            order.flatMap(() => [0, 0, back ? -1 : 1]),
            'VEC3',
          ),
          TEXCOORD_0: attribute(
            order.flatMap((i) => uv[i]),
            'VEC2',
          ),
        },
        material,
      }
    },
  )
  const imageView = append(checker),
    bin = Buffer.concat(parts),
    doc = {
      asset: { version: '2.0', generator: 'Nova Render material conformance' },
      scene: 0,
      scenes: [{ nodes: [0] }],
      nodes: [{ mesh: 0, ...node }],
      meshes: [{ primitives }],
      materials,
      buffers: [{ byteLength: bin.length }],
      bufferViews: views,
      accessors,
      images: [{ bufferView: imageView, mimeType: 'image/png' }],
      textures: [{ source: 0 }],
    }
  const text = Buffer.from(JSON.stringify(doc)),
    json = Buffer.alloc(Math.ceil(text.length / 4) * 4, 32)
  text.copy(json)
  const header = Buffer.alloc(20)
  header.writeUInt32LE(0x46546c67)
  header.writeUInt32LE(2, 4)
  header.writeUInt32LE(28 + json.length + bin.length, 8)
  header.writeUInt32LE(json.length, 12)
  header.writeUInt32LE(0x4e4f534a, 16)
  const chunk = Buffer.alloc(8)
  chunk.writeUInt32LE(bin.length)
  chunk.writeUInt32LE(0x004e4942, 4)
  writeFileSync(`${dir}/${name}.glb`, Buffer.concat([header, json, chunk, bin]))
}
glb('Quad', [material([1, 0.05, 0.02, 1])], [{}])
glb('Mirrored', [material([1, 0.05, 0.02, 1])], [{}], { scale: [-1, 1, 1] })
glb('Back', [material([1, 0.05, 0.02, 1])], [{ back: true }])
const mats = [
  material([0.3, 0.3, 0.3, 1]),
  material([1, 0.05, 0.02, 0.45], 'BLEND', true),
  material([0.02, 0.2, 1, 0.6], 'BLEND', true),
  material([0.2, 1, 0.1, 1], 'MASK', true, true),
]
const planes = [
  { material: 0, z: -0.3, w: 1.6, h: 1.1 },
  { material: 1, x: -0.25, z: 0.2, w: 0.7, h: 0.8 },
  { material: 2, x: 0.3, z: 0.4, w: 0.7, h: 0.7 },
  { material: 3, x: 1.15, z: 0.5, w: 0.3, h: 0.8 },
]
glb('Layers', mats, planes)
glb('LayersReverse', mats, [...planes].reverse())
glb('Mask', [material([1, 0.05, 0.02, 1], 'MASK', true, true)], [{}])
console.log('Material fixtures generated')
