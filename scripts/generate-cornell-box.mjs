// Self-contained Cornell-style scene, authored for Nova. No downloaded geometry.
import { deflateSync } from 'node:zlib'
import { writeFile, mkdir } from 'node:fs/promises'
async function generate(name) {
  const indirect = name === 'CornellIndirect',
    glossy = name === 'CornellGlossy'
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
  if (glossy) palette.push([235, 235, 235, 255])
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
  function png(mode) {
    const header = Buffer.alloc(13)
    header.writeUInt32BE(palette.length * 8)
    header.writeUInt32BE(8, 4)
    header[8] = 8
    header[9] = 6
    const rows = []
    for (let y = 0; y < 8; y++) {
      const row = [0]
      for (let x = 0; x < palette.length * 8; x++) {
        const k = x >> 3
        row.push(
          ...(mode === 2
            ? [255, k === 4 ? 20 : 255, k === 4 ? 255 : 0, 255]
            : mode === 1
              ? k === 3
                ? [255, 255, 255, 255]
                : [0, 0, 0, 255]
              : palette[k]),
        )
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
      uvs.push((color + 0.5) / palette.length, 0.5)
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
      [glossy ? -0.5 : -0.35, 0.995, glossy ? 0.15 : -0.45],
      [glossy ? -0.3 : 0.35, 0.995, glossy ? 0.15 : -0.45],
      [glossy ? -0.3 : 0.35, 0.995, glossy ? 0.35 : 0.2],
      [glossy ? -0.5 : -0.35, 0.995, glossy ? 0.35 : 0.2],
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
  if (!glossy) box(-0.4, 0.35, 0.58, 0.6, 0.58, -0.25)
  if (indirect)
    quad(
      [
        [-0.72, 0.65, -0.82],
        [0.72, 0.65, -0.82],
        [0.72, 0.65, 0.65],
        [-0.72, 0.65, 0.65],
      ],
      [0, 1, 0],
    )
  if (glossy) {
    const point = (t, p) => [Math.sin(t) * Math.cos(p), Math.cos(t), Math.sin(t) * Math.sin(p)]
    function triangle(ns) {
      const ps = ns.map((n) => [-0.4 + n[0] * 0.36, -0.64 + n[1] * 0.36, 0.25 + n[2] * 0.36])
      const e = ps[1].map((x, k) => x - ps[0][k]),
        f = ps[2].map((x, k) => x - ps[0][k]),
        c = [e[1] * f[2] - e[2] * f[1], e[2] * f[0] - e[0] * f[2], e[0] * f[1] - e[1] * f[0]]
      if (Math.hypot(...c) < 1e-8) return
      if (c.reduce((sum, x, k) => sum + x * ns[0][k], 0) < 0) {
        ;[ps[1], ps[2]] = [ps[2], ps[1]]
        ;[ns[1], ns[2]] = [ns[2], ns[1]]
      }
      const start = positions.length / 3
      for (let i = 0; i < 3; i++) {
        positions.push(...ps[i])
        normals.push(...c.map((x) => x / Math.hypot(...c)))
        uvs.push(4.5 / 5, 0.5)
      }
      indices.push(start, start + 1, start + 2)
    }
    for (let y = 0; y < 24; y++)
      for (let x = 0; x < 48; x++) {
        const a = point((y * Math.PI) / 24, (x * 2 * Math.PI) / 48),
          b = point(((y + 1) * Math.PI) / 24, (x * 2 * Math.PI) / 48),
          c = point(((y + 1) * Math.PI) / 24, ((x + 1) * 2 * Math.PI) / 48),
          d = point((y * Math.PI) / 24, ((x + 1) * 2 * Math.PI) / 48)
        triangle([a, b, c])
        triangle([a, c, d])
      }
  }
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
    base = view(png(0)),
    emission = view(png(1))
  const mr = glossy ? view(png(2)) : -1
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
  if (glossy) {
    gltf.materials[0].pbrMetallicRoughness.metallicFactor = 1
    gltf.materials[0].pbrMetallicRoughness.metallicRoughnessTexture = { index: 2 }
    gltf.textures.push({ source: 2 })
    gltf.images.push({ bufferView: mr, mimeType: 'image/png' })
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
  await mkdir(`assets/${name}`, { recursive: true })
  await writeFile(`assets/${name}/${name}.glb`, Buffer.concat([header, j, json, b, bin]))
  console.log(name, indices.length / 3, 'triangles')
}
for (const name of process.argv.slice(2).length ? process.argv.slice(2) : ['CornellBox'])
  await generate(name)
