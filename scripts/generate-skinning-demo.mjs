// Small, original skinned robot: standard glTF joints/weights/inverse binds and two clips.
import { deflateSync } from 'node:zlib'
import { mkdir, writeFile } from 'node:fs/promises'
const nodes = [
    { name: 'Scene', children: [1] },
    { name: 'Robot', mesh: 0, skin: 0 },
  ],
  bones = [],
  world = []
function joint(name, parent, t) {
  const id = nodes.length
  nodes.push({ name, translation: t })
  ;(nodes[parent].children ??= []).push(id)
  bones.push(id)
  world[id] = t.map((v, k) => v + (world[parent]?.[k] || 0))
  return id
}
const hip = joint('Pelvis', 0, [0, 0, 0]),
  spine = joint('Spine', hip, [0, 0.45, 0]),
  head = joint('Head', spine, [0, 0.55, 0]),
  left = joint('Left shoulder', spine, [-0.36, 0.38, 0]),
  leftElbow = joint('Left elbow', left, [-0.36, 0, 0]),
  right = joint('Right shoulder', spine, [0.36, 0.38, 0]),
  rightElbow = joint('Right elbow', right, [0.36, 0, 0]),
  leftHip = joint('Left hip', hip, [-0.18, -0.1, 0]),
  leftKnee = joint('Left knee', leftHip, [0, -0.55, 0]),
  rightHip = joint('Right hip', hip, [0.18, -0.1, 0]),
  rightKnee = joint('Right knee', rightHip, [0, -0.55, 0])
const p = [],
  n = [],
  uv = [],
  j = [],
  w = [],
  indices = []
function box(center, size, bone, color) {
  const [x, y, z] = size.map((v) => v / 2),
    [cx, cy, cz] = center
  for (const [points, normal] of [
    [
      [
        [-x, y, z],
        [x, y, z],
        [x, y, -z],
        [-x, y, -z],
      ],
      [0, 1, 0],
    ],
    [
      [
        [-x, -y, -z],
        [x, -y, -z],
        [x, -y, z],
        [-x, -y, z],
      ],
      [0, -1, 0],
    ],
    [
      [
        [-x, -y, z],
        [x, -y, z],
        [x, y, z],
        [-x, y, z],
      ],
      [0, 0, 1],
    ],
    [
      [
        [x, -y, -z],
        [-x, -y, -z],
        [-x, y, -z],
        [x, y, -z],
      ],
      [0, 0, -1],
    ],
    [
      [
        [-x, -y, -z],
        [-x, -y, z],
        [-x, y, z],
        [-x, y, -z],
      ],
      [-1, 0, 0],
    ],
    [
      [
        [x, -y, z],
        [x, -y, -z],
        [x, y, -z],
        [x, y, z],
      ],
      [1, 0, 0],
    ],
  ]) {
    const start = p.length / 3
    for (const pos of points) {
      p.push(cx + pos[0], cy + pos[1], cz + pos[2])
      n.push(...normal)
      uv.push((color + 0.5) / 4, 0.5)
      // Blend elbow/knee seam vertices across two joints; distal vertices stay rigid.
      const elbow = (bone === leftElbow || bone === rightElbow) && Math.abs(cx + pos[0]) < 0.8
      const knee = (bone === leftKnee || bone === rightKnee) && cy + pos[1] > -0.75
      const parent =
        bone === leftElbow
          ? left
          : bone === rightElbow
            ? right
            : bone === leftKnee
              ? leftHip
              : rightHip
      j.push(bones.indexOf(bone), elbow || knee ? bones.indexOf(parent) : 0, 0, 0)
      w.push(elbow || knee ? 0.5 : 1, elbow || knee ? 0.5 : 0, 0, 0)
    }
    indices.push(start, start + 1, start + 2, start, start + 2, start + 3)
  }
}
box([0, 0.05, 0], [0.45, 0.28, 0.3], hip, 0)
box([0, 0.5, 0], [0.5, 0.6, 0.3], spine, 1)
box([0, 1.17, 0], [0.4, 0.38, 0.34], head, 0)
for (const sign of [-1, 1]) {
  box([sign * 0.54, 0.83, 0], [0.32, 0.18, 0.2], sign < 0 ? left : right, 1)
  box([sign * 0.9, 0.83, 0], [0.3, 0.16, 0.18], sign < 0 ? leftElbow : rightElbow, 0)
  box([sign * 0.18, -0.36, 0], [0.22, 0.48, 0.24], sign < 0 ? leftHip : rightHip, 1)
  box([sign * 0.18, -0.92, 0], [0.2, 0.48, 0.22], sign < 0 ? leftKnee : rightKnee, 0)
  box([sign * 0.18, -1.18, 0.07], [0.24, 0.12, 0.4], sign < 0 ? leftKnee : rightKnee, 2)
  box([sign * 0.085, 1.22, 0.18], [0.055, 0.065, 0.025], head, 2)
}
const data = [],
  views = [],
  accessors = []
let length = 0
function view(bytes) {
  const pad = Buffer.alloc((4 - (length % 4)) % 4)
  data.push(pad)
  length += pad.length
  views.push({ buffer: 0, byteOffset: length, byteLength: bytes.length })
  data.push(bytes)
  length += bytes.length
  return views.length - 1
}
function accessor(values, type, count, componentType = 5126) {
  const array =
      componentType === 5126
        ? new Float32Array(values)
        : componentType === 5123
          ? new Uint16Array(values)
          : new Uint8Array(values),
    a = { bufferView: view(Buffer.from(array.buffer)), componentType, count, type }
  if (type === 'SCALAR' && componentType === 5126) {
    a.min = [Math.min(...values)]
    a.max = [Math.max(...values)]
  }
  if (accessors.length === 0) {
    a.min = [0, 1, 2].map((k) => Math.min(...values.filter((_, i) => i % 3 === k)))
    a.max = [0, 1, 2].map((k) => Math.max(...values.filter((_, i) => i % 3 === k)))
  }
  accessors.push(a)
  return accessors.length - 1
}
const position = accessor(p, 'VEC3', p.length / 3),
  normal = accessor(n, 'VEC3', n.length / 3),
  texcoord = accessor(uv, 'VEC2', uv.length / 2),
  joints = accessor(j, 'VEC4', j.length / 4, 5121),
  weights = accessor(w, 'VEC4', w.length / 4),
  index = accessor(indices, 'SCALAR', indices.length, 5123)
const matrices = bones.flatMap((id) => [
    1,
    0,
    0,
    0,
    0,
    1,
    0,
    0,
    0,
    0,
    1,
    0,
    ...world[id].map((v) => -v),
    1,
  ]),
  inverseBind = accessor(matrices, 'MAT4', bones.length)
const times = Array.from({ length: 17 }, (_, i) => i / 8),
  input = accessor(times, 'SCALAR', times.length)
function rotation(x = 0, y = 0, z = 0) {
  const sx = Math.sin(x / 2),
    cx = Math.cos(x / 2),
    sy = Math.sin(y / 2),
    cy = Math.cos(y / 2),
    sz = Math.sin(z / 2),
    cz = Math.cos(z / 2)
  return [
    sx * cy * cz - cx * sy * sz,
    cx * sy * cz + sx * cy * sz,
    cx * cy * sz - sx * sy * cz,
    cx * cy * cz + sx * sy * sz,
  ]
}
const animations = ['Walk', 'Wave'].map((name) => {
  const samplers = [],
    channels = []
  for (const bone of [
    head,
    left,
    leftElbow,
    right,
    rightElbow,
    leftHip,
    leftKnee,
    rightHip,
    rightKnee,
  ]) {
    const values = times.flatMap((t) => {
      const phase = t * Math.PI,
        s = Math.sin(phase)
      if (name === 'Walk') {
        if (bone === leftHip) return rotation(0.65 * s)
        if (bone === rightHip) return rotation(-0.65 * s)
        if (bone === leftKnee) return rotation(Math.max(0, -s) * 0.9)
        if (bone === rightKnee) return rotation(Math.max(0, s) * 0.9)
        if (bone === left) return rotation(-s * 0.35, 0, 1.2)
        if (bone === right) return rotation(s * 0.35, 0, -1.2)
        return rotation()
      }
      if (bone === right) return rotation(0, 0, 0.45)
      if (bone === rightElbow) return rotation(0, 0, 0.6 + 0.45 * Math.sin(phase * 2))
      if (bone === left) return rotation(0, 0, 1.2)
      if (bone === head) return rotation(0, 0.15 * s, 0)
      return rotation()
    })
    const output = accessor(values, 'VEC4', times.length)
    channels.push({ sampler: samplers.length, target: { node: bone, path: 'rotation' } })
    samplers.push({ input, output, interpolation: 'LINEAR' })
  }
  return { name, samplers, channels }
})
function crc(b) {
  let n = 0xffffffff
  for (const v of b) {
    n ^= v
    for (let i = 0; i < 8; i++) n = (n >>> 1) ^ (n & 1 ? 0xedb88320 : 0)
  }
  return (n ^ 0xffffffff) >>> 0
}
function chunk(name, b) {
  const t = Buffer.from(name),
    h = Buffer.alloc(4),
    c = Buffer.alloc(4)
  h.writeUInt32BE(b.length)
  c.writeUInt32BE(crc(Buffer.concat([t, b])))
  return Buffer.concat([h, t, b, c])
}
const ihdr = Buffer.alloc(13)
ihdr.writeUInt32BE(4)
ihdr.writeUInt32BE(1, 4)
ihdr[8] = 8
ihdr[9] = 6
const png = Buffer.concat([
  Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
  chunk('IHDR', ihdr),
  chunk(
    'IDAT',
    deflateSync(
      Buffer.from([0, 225, 232, 240, 255, 36, 135, 215, 255, 22, 35, 55, 255, 255, 170, 30, 255]),
    ),
  ),
  chunk('IEND', Buffer.alloc(0)),
])
const image = view(png)
let bin = Buffer.concat([...data, Buffer.alloc((4 - (length % 4)) % 4)])
const gltf = {
  asset: { version: '2.0', generator: 'Nova skeletal robot generator' },
  scene: 0,
  scenes: [{ nodes: [0] }],
  nodes,
  skins: [{ joints: bones, inverseBindMatrices: inverseBind, skeleton: hip }],
  animations,
  meshes: [
    {
      primitives: [
        {
          attributes: {
            POSITION: position,
            NORMAL: normal,
            TEXCOORD_0: texcoord,
            JOINTS_0: joints,
            WEIGHTS_0: weights,
          },
          indices: index,
          material: 0,
        },
      ],
    },
  ],
  materials: [
    {
      pbrMetallicRoughness: {
        baseColorTexture: { index: 0 },
        metallicFactor: 0,
        roughnessFactor: 0.65,
      },
    },
  ],
  textures: [{ source: 0 }],
  images: [{ bufferView: image, mimeType: 'image/png' }],
  buffers: [{ byteLength: bin.length }],
  bufferViews: views,
  accessors,
}
let json = Buffer.from(JSON.stringify(gltf))
json = Buffer.concat([json, Buffer.alloc((4 - (json.length % 4)) % 4, 32)])
const h = Buffer.alloc(12),
  a = Buffer.alloc(8),
  b = Buffer.alloc(8)
h.writeUInt32LE(0x46546c67)
h.writeUInt32LE(2, 4)
h.writeUInt32LE(28 + json.length + bin.length, 8)
a.writeUInt32LE(json.length)
a.writeUInt32LE(0x4e4f534a, 4)
b.writeUInt32LE(bin.length)
b.writeUInt32LE(0x004e4942, 4)
await mkdir('assets/SkinningRobot', { recursive: true })
await writeFile('assets/SkinningRobot/SkinningRobot.glb', Buffer.concat([h, a, json, b, bin]))
console.log('SkinningRobot:', bones.length, 'joints,', indices.length / 3, 'triangles, 2 clips')
