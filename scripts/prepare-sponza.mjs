// Offline-only tool. npm install --prefix /tmp/nova-asset-tools meshoptimizer@1.3.0
import { readFile, writeFile, mkdir, copyFile } from 'node:fs/promises'
import { createHash } from 'node:crypto'
const { MeshoptSimplifier } = await import(process.env.NOVA_MESHOPT_MODULE || 'meshoptimizer')
const source = process.argv[2] || '/tmp/nova-sponza-source',
  target = Number(process.argv[3] || 180000)
const json = JSON.parse(await readFile(source + '/NewSponza_Main_glTF_003.gltf', 'utf8')),
  bin = await readFile(source + '/NewSponza_Main_glTF_003.bin')
const identity = () => [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
const multiply = (a, b) =>
  Array.from({ length: 16 }, (_, i) => {
    let sum = 0
    for (let k = 0; k < 4; k++) sum += a[(i % 4) + k * 4] * b[k + Math.floor(i / 4) * 4]
    return sum
  })
function matrix(n) {
  if (n.matrix) return n.matrix
  const [x, y, z, w] = n.rotation || [0, 0, 0, 1],
    [sx, sy, sz] = n.scale || [1, 1, 1],
    [tx, ty, tz] = n.translation || [0, 0, 0]
  return [
    (1 - 2 * y * y - 2 * z * z) * sx,
    (2 * x * y + 2 * z * w) * sx,
    (2 * x * z - 2 * y * w) * sx,
    0,
    (2 * x * y - 2 * z * w) * sy,
    (1 - 2 * x * x - 2 * z * z) * sy,
    (2 * y * z + 2 * x * w) * sy,
    0,
    (2 * x * z + 2 * y * w) * sz,
    (2 * y * z - 2 * x * w) * sz,
    (1 - 2 * x * x - 2 * y * y) * sz,
    0,
    tx,
    ty,
    tz,
    1,
  ]
}
function accessor(id) {
  const a = json.accessors[id],
    v = json.bufferViews[a.bufferView],
    components = { VEC3: 3, SCALAR: 1 }[a.type],
    bytes = { 5123: 2, 5125: 4, 5126: 4 }[a.componentType],
    out =
      a.componentType === 5126
        ? new Float32Array(a.count * components)
        : new Uint32Array(a.count * components)
  for (let i = 0; i < a.count; i++)
    for (let k = 0; k < components; k++) {
      const offset =
        (v.byteOffset || 0) +
        (a.byteOffset || 0) +
        i * (v.byteStride || bytes * components) +
        k * bytes
      out[i * components + k] =
        a.componentType === 5126
          ? bin.readFloatLE(offset)
          : bytes === 2
            ? bin.readUInt16LE(offset)
            : bin.readUInt32LE(offset)
    }
  return out
}
const parts = []
let total = 0
function visit(id, parent) {
  const n = json.nodes[id],
    m = multiply(parent, matrix(n))
  if (n.mesh !== undefined)
    for (const primitive of json.meshes[n.mesh].primitives) {
      if (primitive.mode !== undefined && primitive.mode !== 4) continue
      const p = accessor(primitive.attributes.POSITION),
        indices = accessor(primitive.indices)
      for (let i = 0; i < p.length; i += 3) {
        const x = p[i],
          y = p[i + 1],
          z = p[i + 2]
        for (let k = 0; k < 3; k++) p[i + k] = m[k] * x + m[k + 4] * y + m[k + 8] * z + m[k + 12]
      }
      parts.push({ p, indices, name: n.name })
      total += indices.length / 3
    }
  for (const c of n.children || []) visit(c, m)
}
for (const id of json.scenes[json.scene || 0].nodes) visit(id, identity())
let min = [Infinity, Infinity, Infinity],
  max = [-Infinity, -Infinity, -Infinity]
for (const { p } of parts)
  for (let i = 0; i < p.length; i++) {
    min[i % 3] = Math.min(min[i % 3], p[i])
    max[i % 3] = Math.max(max[i % 3], p[i])
  }
await MeshoptSimplifier.ready
const ratio = target / total,
  positions = [],
  indices = []
let maxError = 0
for (const part of parts) {
  const remap = MeshoptSimplifier.generatePositionRemap(part.p, 3),
    welded = part.indices.map((i) => remap[i])
  const [simple, error] = MeshoptSimplifier.simplify(
    welded,
    part.p,
    3,
    Math.min(welded.length, Math.max(24, Math.floor((welded.length * ratio) / 3) * 3)),
    0.003,
    ['Permissive'],
  )
  maxError = Math.max(maxError, error)
  const [compact, count] = MeshoptSimplifier.compactMesh(simple),
    base = positions.length / 3,
    output = new Float32Array(count * 3)
  for (let i = 0; i < compact.length; i++)
    if (compact[i] !== 0xffffffff)
      for (let k = 0; k < 3; k++) output[compact[i] * 3 + k] = part.p[i * 3 + k]
  for (const v of output) positions.push(v)
  for (const i of simple) indices.push(base + i)
}
const pbuf = Buffer.from(new Float32Array(positions).buffer),
  ibuf = Buffer.from(new Uint32Array(indices).buffer),
  binary = Buffer.concat([pbuf, ibuf])
const gltf = {
  asset: { version: '2.0', generator: 'Nova Intel Sponza whitebox simplification' },
  scene: 0,
  scenes: [{ nodes: [0] }],
  nodes: [{ mesh: 0 }],
  meshes: [{ primitives: [{ attributes: { POSITION: 0 }, indices: 1, material: 0 }] }],
  materials: [
    {
      name: 'White matte',
      doubleSided: true,
      pbrMetallicRoughness: {
        baseColorFactor: [0.75, 0.75, 0.75, 1],
        metallicFactor: 0,
        roughnessFactor: 1,
      },
    },
  ],
  buffers: [{ byteLength: binary.length }],
  bufferViews: [
    { buffer: 0, byteOffset: 0, byteLength: pbuf.length },
    { buffer: 0, byteOffset: pbuf.length, byteLength: ibuf.length },
  ],
  accessors: [
    { bufferView: 0, componentType: 5126, count: positions.length / 3, type: 'VEC3', min, max },
    { bufferView: 1, componentType: 5125, count: indices.length, type: 'SCALAR' },
  ],
}
let text = Buffer.from(JSON.stringify(gltf))
text = Buffer.concat([text, Buffer.alloc((4 - (text.length % 4)) % 4, 32)])
const header = Buffer.alloc(28)
header.writeUInt32LE(0x46546c67)
header.writeUInt32LE(2, 4)
header.writeUInt32LE(28 + text.length + binary.length, 8)
header.writeUInt32LE(text.length, 12)
header.writeUInt32LE(0x4e4f534a, 16)
const bh = Buffer.alloc(8)
bh.writeUInt32LE(binary.length)
bh.writeUInt32LE(0x004e4942, 4)
const glb = Buffer.concat([header.subarray(0, 20), text, bh, binary])
await mkdir('assets/SponzaWhite', { recursive: true })
await writeFile('assets/SponzaWhite/SponzaWhite.glb', glb)
await copyFile(source + '/credits_license.txt', 'assets/SponzaWhite/credits_license.txt')
const report = {
  simplifier: 'meshoptimizer 1.3.0',
  targetTriangles: target,
  maxRelativeErrorLimit: 0.003,
  source: 'https://cdrdv2.intel.com/v1/dl/getContent/830833',
  sourceGeometrySHA256: createHash('sha256').update(bin).digest('hex'),
  sourceTriangles: total,
  triangles: indices.length / 3,
  vertices: positions.length / 3,
  bytes: glb.length,
  sha256: createHash('sha256').update(glb).digest('hex'),
  min,
  max,
  maxRelativeError: maxError,
}
await writeFile('assets/SponzaWhite/manifest.json', JSON.stringify(report, null, 2) + '\n')
console.log(report)
