import { modelVertexFloats } from './ModelVertex.js'
import { decodeMaterialImage } from './MaterialImage.js'
import { packMaterials, validateMaterial } from './MaterialData.js'
import { cameraFromOrbit, cameraBasis } from './Camera.js'
import { readAnimation } from './AnimationData.js'
import { defaultEnvironment } from './EnvironmentData.js'
import { defaultDirectionalLight } from './Lighting.js'

const coatTextureKeys = ['clearcoatTexture', 'clearcoatRoughnessTexture', 'clearcoatNormalTexture']
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
      (e) =>
        ![
          'KHR_materials_transmission',
          'KHR_materials_ior',
          'KHR_materials_specular',
          'KHR_materials_emissive_strength',
          'KHR_xmp_json_ld',
          'KHR_xmp',
          'KHR_materials_clearcoat',
        ].includes(e),
    ) ||
    json.buffers.length !== 1 ||
    json.buffers[0].uri
  )
    throw Error('Expected embedded GLB data and supported glTF extensions')
  json.materials ??= []
  const defaultMaterial = json.materials.length
  const animation = readAnimation(json, bin)
  const raw = new DataView(bin)
  function element(index, i, type, components, color = false) {
    const a = json.accessors[index],
      v = json.bufferViews[a.bufferView]
    const size = { 5121: 1, 5123: 2, 5125: 4, 5126: 4 }[a.componentType],
      stride = v.byteStride || size * components
    const uvDivisor =
      (type === 'VEC2' || color) && a.normalized ? { 5121: 255, 5123: 65535 }[a.componentType] : 0
    const start = (v.byteOffset || 0) + (a.byteOffset || 0) + i * stride
    if (
      a.sparse ||
      (a.normalized && !uvDivisor) ||
      ((type === 'VEC2' || color) && a.componentType !== 5126 && !uvDivisor) ||
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
            ? raw.getUint16(start + k * size, true) / (uvDivisor || 1)
            : raw.getUint8(start + k) / (uvDivisor || 1),
    )
  }
  function readColor(index, vertex) {
    if (index === undefined) return [1, 1, 1, 1]
    const type = json.accessors[index].type
    if (type !== 'VEC3' && type !== 'VEC4') throw Error('COLOR_0 requires VEC3 or VEC4')
    const color = element(index, vertex, type, type === 'VEC3' ? 3 : 4, true)
    if (color.some((v) => !Number.isFinite(v))) throw Error('Invalid COLOR_0 value')
    if (color.length === 3) color.push(1)
    return color.map((v) => Math.max(0, Math.min(1, v)))
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
    if (Math.abs(det) <= 1e-10 * Math.hypot(...a) * Math.hypot(...b) * Math.hypot(...c))
      throw Error('Degenerate node transform')
    if (node.mesh !== undefined)
      for (const p of json.meshes[node.mesh].primitives) {
        const mode = p.mode ?? 4
        if (![4, 5, 6].includes(mode) || p.targets?.length)
          throw Error('Only triangle primitives without morph targets are supported')
        if (p.material !== undefined && (!Number.isInteger(p.material) || p.material < 0 || p.material >= defaultMaterial))
          throw Error('Invalid glTF material index')
        const materialIndex = p.material ?? defaultMaterial
        if (p.material === undefined && json.materials.length === defaultMaterial)
          json.materials.push({})
        const mat = json.materials[materialIndex],
          pbr = mat.pbrMetallicRoughness || {}
        if (
          p.attributes.TEXCOORD_0 === undefined &&
          [
            pbr.baseColorTexture,
            pbr.metallicRoughnessTexture,
            mat.normalTexture,
            mat.occlusionTexture && (mat.occlusionTexture.texCoord ?? 0) === 0,
            mat.emissiveTexture,
          ].some(Boolean)
        )
          throw Error('Textured material requires TEXCOORD_0')
        if (mat.occlusionTexture?.texCoord === 1 && p.attributes.TEXCOORD_1 === undefined)
          throw Error('AO texture requires TEXCOORD_1')
        const extensions = mat.extensions || {}
        for (const key of coatTextureKeys) {
          const info = extensions.KHR_materials_clearcoat?.[key]
          if (info && p.attributes['TEXCOORD_' + (info.texCoord ?? 0)] === undefined)
            throw Error('Clearcoat texture requires its declared TEXCOORD set')
        }
        const transmission = extensions.KHR_materials_transmission?.transmissionFactor ?? 0,
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
        const inputCount = json.accessors[p.indices ?? p.attributes.POSITION].count
        if (!Number.isSafeInteger(inputCount) || inputCount < 3 || (mode === 4 && inputCount % 3))
          throw Error('Invalid triangle mesh')
        const count = mode === 4 ? inputCount : (inputCount - 2) * 3
        if (vertices.length / modelVertexFloats + count > 10000000)
          throw Error('Oversized triangle mesh')
        function vertexIndex(i) {
          const triangle = Math.floor(i / 3),
            corner = i % 3
          if (mode === 5)
            i = triangle + (triangle % 2 && corner < 2 ? 1 - corner : corner)
          else if (mode === 6)
            i = corner === 0 ? 0 : triangle + corner
          return p.indices === undefined ? i : element(p.indices, i, 'SCALAR', 1)[0]
        }
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
          if (mode !== 4 && i % 3 === 0 && p.indices !== undefined) {
            const a = vertexIndex(i), b = vertexIndex(i + 1), c = vertexIndex(i + 2)
            if (a === b || b === c || c === a) {
              i += 2
              continue
            }
          }
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
                : element(p.attributes.TEXCOORD_0, index, 'VEC2', 2),
            uv1 =
              p.attributes.TEXCOORD_1 === undefined
                ? uv
                : element(p.attributes.TEXCOORD_1, index, 'VEC2', 2)
          const world = pos.map(
            (_, r) => m[r] * pos[0] + m[4 + r] * pos[1] + m[8 + r] * pos[2] + m[12 + r],
          )
          const n = normalize(
            normal.map(
              (_, r) =>
                (cof[0][r] * normal[0] + cof[1][r] * normal[1] + cof[2][r] * normal[2]) / det,
            ),
          )
          if (![...world, ...n, ...uv, ...uv1].every(Number.isFinite))
            throw Error('Non-finite model data')
          for (let k = 0; k < 3; k++) {
            min[k] = Math.min(min[k], world[k])
            max[k] = Math.max(max[k], world[k])
          }
          vertices.push(
            ...world,
            materialIndex + 1,
            ...n,
            transmission,
            ...uv,
            p.attributes.NORMAL === undefined ? 1 : 0,
            ior,
            ...uv1,
            0,
            0,
            ...readColor(p.attributes.COLOR_0, index),
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
  for (let i = 0; i < vertices.length; i += modelVertexFloats)
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
    const emission = material.extensions?.KHR_materials_emissive_strength
    let emissiveStrength = 1
    if (emission !== undefined) {
      if (!emission || typeof emission !== 'object' || Array.isArray(emission))
        throw Error('Invalid KHR_materials_emissive_strength')
      if ('emissiveStrength' in emission) {
        emissiveStrength = emission.emissiveStrength
        if (!Number.isFinite(emissiveStrength) || emissiveStrength < 0)
          throw Error('Invalid glTF emissiveStrength')
      }
    }
    const specular = material.extensions?.KHR_materials_specular
    if (specular !== undefined) {
      if (!specular || typeof specular !== 'object' || Array.isArray(specular))
        throw Error('Invalid KHR_materials_specular')
      if ('specularTexture' in specular || 'specularColorTexture' in specular)
        throw Error('KHR_materials_specular textures are not supported')
      if ('specularFactor' in specular && typeof specular.specularFactor !== 'number')
        throw Error('Invalid glTF specularFactor')
      if ('specularColorFactor' in specular && !Array.isArray(specular.specularColorFactor))
        throw Error('Invalid glTF specularColorFactor')
    }
    const coat = material.extensions?.KHR_materials_clearcoat
    if (coat !== undefined) {
      if (!coat || typeof coat !== 'object' || Array.isArray(coat))
        throw Error('Invalid KHR_materials_clearcoat')
      for (const key of coatTextureKeys)
        if (key in coat) {
          const info = coat[key]
          if (!info || !Number.isInteger(info.index) || info.index < 0)
            throw Error('Invalid clearcoat texture index')
          if (![0, 1].includes(info.texCoord ?? 0))
            throw Error('Clearcoat coordinate set must be 0 or 1')
        }
      for (const key of ['clearcoatFactor', 'clearcoatRoughnessFactor'])
        if (key in coat && typeof coat[key] !== 'number') throw Error('Invalid glTF ' + key)
    }
    if (!['OPAQUE', 'MASK', 'BLEND'].includes(material.alphaMode || 'OPAQUE'))
      throw Error('Invalid glTF alpha mode')
    const textures = [
      pbr.baseColorTexture,
      material.normalTexture,
      pbr.metallicRoughnessTexture,
      material.occlusionTexture,
      material.emissiveTexture,
      ...coatTextureKeys.map((key) => coat?.[key]),
    ]
    const fallback = [
      [255, 255, 255, 255],
      [128, 128, 255, 255],
      [255, 255, 255, 255],
      [255, 255, 255, 255],
      [0, 0, 0, 255],
      [255, 255, 255, 255],
      [255, 255, 255, 255],
      [128, 128, 255, 255],
    ]
    const images = []
    try {
      for (let i = 0; i < textures.length; i++) {
        if (!textures[i]) {
          images.push({ width: 1, height: 1, pixels: new Uint8Array(fallback[i]) })
          continue
        }
        if (i < 5 && i !== 3 && (textures[i].texCoord ?? 0) !== 0)
          throw Error('Only AO textures support TEXCOORD_1')
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
          specularFactor: specular?.specularFactor,
          clearcoatFactor: coat?.clearcoatFactor,
          clearcoatRoughnessFactor: coat?.clearcoatRoughnessFactor,
          clearcoatNormalScale: coat?.clearcoatNormalTexture?.scale,
          clearcoatTexCoords: coatTextureKeys.map((key) => coat?.[key]?.texCoord ?? 0),
          specularColorFactor: specular?.specularColorFactor,
          alphaMode: material.alphaMode ?? 'OPAQUE',
          alphaCutoff: material.alphaCutoff ?? 0.5,
          doubleSided: material.doubleSided ?? false,
          normalScale: material.normalTexture?.scale ?? 1,
          occlusionStrength: material.occlusionTexture?.strength ?? 1,
          occlusionTexCoord: material.occlusionTexture?.texCoord ?? 0,
          emissive: (material.emissiveFactor || [0, 0, 0]).map((v) => v * emissiveStrength),
          baseColorTexture: textures[0] ? images[0] : null,
          normalTexture: textures[1] ? images[1] : null,
          metallicRoughnessTexture: textures[2] ? images[2] : null,
          occlusionTexture: textures[3] ? images[3] : null,
          emissiveTexture: textures[4] ? images[4] : null,
          clearcoatTexture: textures[5] ? images[5] : null,
          clearcoatRoughnessTexture: textures[6] ? images[6] : null,
          clearcoatNormalTexture: textures[7] ? images[7] : null,
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
    [255, 255, 255, 255],
    [255, 255, 255, 255],
    [128, 128, 255, 255],
  ]
  return {
    vertices: new Float32Array(0),
    images: colors.map((color) => ({ width: 1, height: 1, pixels: new Uint8Array(color) })),
    baseColor: [1, 1, 1, 1],
    material: [1, 1, 1, 1],
    emissive: [0, 0, 0, 0],
  }
}
