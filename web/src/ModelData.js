import { decodeMaterialImage } from './MaterialImage.js'
import { packMaterials, validateMaterial } from './MaterialData.js'
import { cameraFromOrbit, cameraBasis } from './Camera.js'
import { readAnimation } from './AnimationData.js'
import { defaultEnvironment } from './EnvironmentData.js'
import { defaultDirectionalLight } from './Lighting.js'
const identity = () => [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
export function multiply(a, b) {
  const out = Array(16).fill(0)
  for (let c = 0; c < 4; c++)
    for (let r = 0; r < 4; r++)
      for (let k = 0; k < 4; k++) out[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k]
  return out
}
const cross = (a, b) => [
  a[1] * b[2] - a[2] * b[1],
  a[2] * b[0] - a[0] * b[2],
  a[0] * b[1] - a[1] * b[0],
]
const dot = (a, b) => a.reduce((s, x, i) => s + x * b[i], 0)
const normalize = (a) => {
  const n = Math.hypot(...a)
  if (!n) throw Error('Degenerate model normal')
  return a.map((x) => x / n)
}
export function nodeMatrix(n) {
  if (n.matrix) return n.matrix
  const [x, y, z, w] = n.rotation || [0, 0, 0, 1],
    s = n.scale || [1, 1, 1],
    t = n.translation || [0, 0, 0]
  return [
    (1 - 2 * y * y - 2 * z * z) * s[0],
    (2 * x * y + 2 * z * w) * s[0],
    (2 * x * z - 2 * y * w) * s[0],
    0,
    (2 * x * y - 2 * z * w) * s[1],
    (1 - 2 * x * x - 2 * z * z) * s[1],
    (2 * y * z + 2 * x * w) * s[1],
    0,
    (2 * x * z + 2 * y * w) * s[2],
    (2 * y * z - 2 * x * w) * s[2],
    (1 - 2 * x * x - 2 * y * y) * s[2],
    0,
    ...t,
    1,
  ]
}
export async function loadGLB(url) {
  const response = await fetch(url)
  if (!response.ok) throw Error('Load GLB: HTTP ' + response.status)
  const bytes = await response.arrayBuffer(),
    dv = new DataView(bytes)
  if (
    bytes.byteLength < 20 ||
    dv.getUint32(0, true) !== 0x46546c67 ||
    dv.getUint32(4, true) !== 2 ||
    dv.getUint32(8, true) !== bytes.byteLength
  )
    throw Error('Expected glTF 2.0 GLB')
  let json, bin
  for (let offset = 12; offset < bytes.byteLength; ) {
    if (offset + 8 > bytes.byteLength) throw Error('Truncated GLB')
    const size = dv.getUint32(offset, true),
      type = dv.getUint32(offset + 4, true)
    offset += 8
    if (offset + size > bytes.byteLength) throw Error('Invalid GLB chunk')
    if (type === 0x4e4f534a)
      json = JSON.parse(new TextDecoder().decode(new Uint8Array(bytes, offset, size)))
    if (type === 0x004e4942) bin = bytes.slice(offset, offset + size)
    offset += size
  }
  if (
    !json ||
    !bin ||
    [...(json.extensionsRequired || []), ...(json.extensionsUsed || [])].some(
      (e) => !['KHR_materials_transmission', 'KHR_materials_ior'].includes(e),
    ) ||
    json.buffers.length !== 1 ||
    json.buffers[0].uri
  )
    throw Error('Only core GLB with embedded data and no extensions is supported')
  const animation = readAnimation(json, bin)
  const raw = new DataView(bin)
  function element(index, i, type, components) {
    const a = json.accessors[index],
      v = json.bufferViews[a.bufferView]
    const size = { 5121: 1, 5123: 2, 5125: 4, 5126: 4 }[a.componentType],
      stride = v.byteStride || size * components
    const start = (v.byteOffset || 0) + (a.byteOffset || 0) + i * stride
    if (
      a.sparse ||
      a.normalized ||
      a.type !== type ||
      i >= a.count ||
      !size ||
      v.buffer !== 0 ||
      start + size * components > (v.byteOffset || 0) + v.byteLength ||
      start + size * components > bin.byteLength
    )
      throw Error('Unsupported or out-of-bounds accessor')
    return Array.from({ length: components }, (_, k) =>
      a.componentType === 5126
        ? raw.getFloat32(start + k * size, true)
        : a.componentType === 5125
          ? raw.getUint32(start + k * size, true)
          : a.componentType === 5123
            ? raw.getUint16(start + k * size, true)
            : raw.getUint8(start + k),
    )
  }
  const vertices = [],
    min = [Infinity, Infinity, Infinity],
    max = [-Infinity, -Infinity, -Infinity],
    active = new Set()
  function visit(index, parent) {
    if (!json.nodes[index] || active.has(index)) throw Error('Invalid scene graph')
    active.add(index)
    const meshNodeIndex = index
    const node = json.nodes[index],
      m = multiply(parent, nodeMatrix(node)),
      a = m.slice(0, 3),
      b = m.slice(4, 7),
      c = m.slice(8, 11)
    const cof = [cross(b, c), cross(c, a), cross(a, b)],
      det = dot(a, cof[0])
    if (Math.abs(det) < 1e-10) throw Error('Degenerate node transform')
    if (node.mesh !== undefined)
      for (const p of json.meshes[node.mesh].primitives) {
        if ((p.mode ?? 4) !== 4 || p.targets)
          throw Error('Only triangle primitives without morph targets are supported')
        if (p.material === undefined) throw Error('Missing material')
        const mat = json.materials[p.material],
          pbr = mat.pbrMetallicRoughness || {}
        if (
          p.attributes.TEXCOORD_0 === undefined &&
          [
            pbr.baseColorTexture,
            pbr.metallicRoughnessTexture,
            mat.normalTexture,
            mat.occlusionTexture,
            mat.emissiveTexture,
          ].some(Boolean)
        )
          throw Error('Textured material requires TEXCOORD_0')
        const extensions = json.materials[p.material].extensions || {},
          transmission = extensions.KHR_materials_transmission?.transmissionFactor ?? 0,
          ior = extensions.KHR_materials_ior?.ior ?? 1.5
        if (extensions.KHR_materials_transmission?.transmissionTexture)
          throw Error('Transmission textures are not yet supported')
        if (
          !Number.isFinite(transmission) ||
          transmission < 0 ||
          transmission > 1 ||
          !Number.isFinite(ior) ||
          ior < 1.01 ||
          ior > 3
        )
          throw Error('Dielectric requires transmission 0..1 and IOR 1.01..3')
        if (
          p.indices !== undefined &&
          ![5121, 5123, 5125].includes(json.accessors[p.indices].componentType)
        )
          throw Error('Invalid index format')
        const count = json.accessors[p.indices ?? p.attributes.POSITION].count
        const vertexIndex = (i) =>
          p.indices === undefined ? i : element(p.indices, i, 'SCALAR', 1)[0]
        if (count % 3 || vertices.length / 12 + count > 10000000)
          throw Error('Invalid or oversized mesh')
        function faceNormal(i) {
          const first = i - (i % 3),
            positions = [0, 1, 2].map((k) =>
              element(p.attributes.POSITION, vertexIndex(first + k), 'VEC3', 3),
            )
          return normalize(
            cross(
              positions[1].map((v, k) => v - positions[0][k]),
              positions[2].map((v, k) => v - positions[0][k]),
            ),
          )
        }
        for (let i = 0; i < count; i++) {
          const index = vertexIndex(
              det < 0 ? i - (i % 3) + (i % 3 === 1 ? 2 : i % 3 === 2 ? 1 : 0) : i,
            ),
            pos = element(p.attributes.POSITION, index, 'VEC3', 3),
            normal =
              p.attributes.NORMAL !== undefined
                ? element(p.attributes.NORMAL, index, 'VEC3', 3)
                : faceNormal(i),
            uv =
              p.attributes.TEXCOORD_0 === undefined
                ? [0, 0]
                : element(p.attributes.TEXCOORD_0, index, 'VEC2', 2)
          const world = pos.map(
            (_, r) => m[r] * pos[0] + m[4 + r] * pos[1] + m[8 + r] * pos[2] + m[12 + r],
          )
          const n = normalize(
            normal.map(
              (_, r) =>
                (cof[0][r] * normal[0] + cof[1][r] * normal[1] + cof[2][r] * normal[2]) / det,
            ),
          )
          if (![...world, ...n, ...uv].every(Number.isFinite)) throw Error('Non-finite model data')
          for (let k = 0; k < 3; k++) {
            min[k] = Math.min(min[k], world[k])
            max[k] = Math.max(max[k], world[k])
          }
          vertices.push(
            ...world,
            p.material + 1,
            ...n,
            transmission,
            ...uv,
            p.attributes.NORMAL === undefined ? 1 : 0,
            ior,
          )
          animation?.addVertex(node.skin ?? -1, p, index, m, meshNodeIndex, normal)
        }
      }
    for (const child of node.children || []) visit(child, m)
    active.delete(index)
  }
  for (const root of json.scenes[json.scene || 0].nodes) visit(root, identity())
  const size = Math.max(...max.map((x, i) => x - min[i]))
  if (!vertices.length || !Number.isFinite(size) || size <= 0) throw Error('Empty model')
  for (let i = 0; i < vertices.length; i += 12)
    for (let k = 0; k < 3; k++)
      vertices[i + k] = ((vertices[i + k] - (min[k] + max[k]) * 0.5) * 2) / size
  if (animation) {
    animation.center = min.map((v, k) => (v + max[k]) * 0.5)
    animation.scale = 2 / size
  }
  if (json.materials.length > 128) throw Error('At most 128 material slots are supported')
  const materials = []
  for (const material of json.materials) {
    const pbr = material.pbrMetallicRoughness || {}
    if (!['OPAQUE', 'MASK', 'BLEND'].includes(material.alphaMode || 'OPAQUE'))
      throw Error('Invalid glTF alpha mode')
    const textures = [
      pbr.baseColorTexture,
      material.normalTexture,
      pbr.metallicRoughnessTexture,
      material.occlusionTexture,
      material.emissiveTexture,
    ]
    const fallback = [
      [255, 255, 255, 255],
      [128, 128, 255, 255],
      [255, 255, 255, 255],
      [255, 255, 255, 255],
      [0, 0, 0, 255],
    ]
    const images = []
    try {
      for (let i = 0; i < 5; i++) {
        if (!textures[i]) {
          images.push({ width: 1, height: 1, pixels: new Uint8Array(fallback[i]) })
          continue
        }
        if ((textures[i].texCoord || 0) !== 0) throw Error('Only TEXCOORD_0 is supported')
        const image = json.images[json.textures[textures[i].index].source]
        if (image.bufferView === undefined) throw Error('GLB images must be embedded')
        const view = json.bufferViews[image.bufferView],
          offset = view.byteOffset || 0
        if (offset + view.byteLength > bin.byteLength) throw Error('Image out of bounds')
        images.push(
          await decodeMaterialImage(
            new Blob([bin.slice(offset, offset + view.byteLength)], { type: image.mimeType }),
          ),
        )
      }
      materials.push(
        validateMaterial({
          name: material.name || '',
          baseColor: pbr.baseColorFactor || [1, 1, 1, 1],
          metallic: pbr.metallicFactor ?? 1,
          roughness: pbr.roughnessFactor ?? 1,
          alphaMode: material.alphaMode ?? 'OPAQUE',
          alphaCutoff: material.alphaCutoff ?? 0.5,
          doubleSided: material.doubleSided ?? false,
          normalScale: material.normalTexture?.scale ?? 1,
          occlusionStrength: material.occlusionTexture?.strength ?? 1,
          emissive: material.emissiveFactor || [0, 0, 0],
          baseColorTexture: textures[0] ? images[0] : null,
          normalTexture: textures[1] ? images[1] : null,
          metallicRoughnessTexture: textures[2] ? images[2] : null,
          occlusionTexture: textures[3] ? images[3] : null,
          emissiveTexture: textures[4] ? images[4] : null,
        }),
      )
    } catch (e) {
      for (const im of images) im.bitmap?.close()
      throw e
    }
  }
  return packMaterials({
    animation,
    vertices: new Float32Array(vertices),
    sourceMinimum: min,
    sourceMaximum: max,
    materials,
  })
}

export function validateCamera(c) {
  if (
    c.target !== undefined &&
    (!Array.isArray(c.target) || c.target.length !== 3 || !c.target.every(Number.isFinite))
  )
    throw Error('Camera target must contain three finite values')
  if (
    ![c.yaw, c.pitch, c.distance, c.exposure].every(Number.isFinite) ||
    Math.abs(c.pitch) > 85 ||
    c.distance < 1.5 ||
    c.distance > 100 ||
    c.exposure <= 0 ||
    c.exposure > 16
  )
    throw Error('Invalid orbit camera (pitch +/-85, distance 1.5..100, exposure 0..16)')
}
export function sceneUniforms(
  data,
  camera,
  aspect,
  lighting = 'unlit',
  light = defaultDirectionalLight(),
  environment = null,
  settings = defaultEnvironment(),
) {
  const {
    c,
    right: s,
    up: u,
    forward: f,
  } = cameraBasis(camera.position ? camera : cameraFromOrbit(camera))
  const eye = c.position
  const view = [
    s[0],
    u[0],
    -f[0],
    0,
    s[1],
    u[1],
    -f[1],
    0,
    s[2],
    u[2],
    -f[2],
    0,
    -dot(s, eye),
    -dot(u, eye),
    dot(f, eye),
    1,
  ]
  const t = 1 / Math.tan((c.verticalFov * Math.PI) / 360),
    near = c.nearPlane,
    far = c.farPlane,
    half = c.orthographicHeight * 0.5
  const projection =
    c.projection === 'orthographic'
      ? [
          1 / (half * aspect),
          0,
          0,
          0,
          0,
          1 / half,
          0,
          0,
          0,
          0,
          1 / (near - far),
          0,
          0,
          0,
          near / (near - far),
          1,
        ]
      : [
          t / aspect,
          0,
          0,
          0,
          0,
          t,
          0,
          0,
          0,
          0,
          far / (near - far),
          -1,
          0,
          0,
          (near * far) / (near - far),
          0,
        ]
  const values = new Float32Array([
    ...multiply(projection, view),
    ...eye,
    c.exposure,
    ...data.baseColor,
    ...data.material,
    ...data.emissive.slice(0, 3),
    lighting === 'pbr' ? 1 : 0,
    ...light.direction.map((v) => v / Math.hypot(...light.direction)),
    0,
    ...light.color,
    light.intensity,
    ...s,
    (c.projection === 'orthographic' ? half : 1 / t) * aspect,
    ...u,
    c.projection === 'orthographic' ? -half : 1 / t,
    ...f,
    environment ? 1 : 0,
    Math.sin((settings.rotation * Math.PI) / 180),
    Math.cos((settings.rotation * Math.PI) / 180),
    settings.intensity,
    environment ? environment.mips.length - 1 : 0,
    settings.backgroundIntensity,
    settings.showBackground ? 1 : 0,
    0,
    data.skinInputs?.byteLength ? 1 : 0,
    ...(environment?.irradiance || new Float32Array(36)).map((v, i) => (i === 3 ? 0 : v)),
  ])
  values[67] = near
  values[71] = far
  return values
}

export function emptyModelData() {
  const colors = [
    [255, 255, 255, 255],
    [128, 128, 255, 255],
    [255, 255, 255, 255],
    [255, 255, 255, 255],
    [0, 0, 0, 255],
  ]
  return {
    vertices: new Float32Array(0),
    images: colors.map((color) => ({ width: 1, height: 1, pixels: new Uint8Array(color) })),
    baseColor: [1, 1, 1, 1],
    material: [1, 1, 1, 1],
    emissive: [0, 0, 0, 0],
  }
}
