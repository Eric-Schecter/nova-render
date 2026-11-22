import { modelVertexFloats, materialSlotLimit } from './ModelVertex.js'
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
const srgbLinear = (value) =>
  value <= 0.04045 ? value / 12.92 : ((value + 0.055) / 1.055) ** 2.4
const linearSrgb = (value) =>
  value <= 0.0031308 ? 12.92 * value : 1.055 * value ** (1 / 2.4) - 0.055
const clampUnit = (value) => Math.min(1, Math.max(0, value))

async function convertSpecularGlossiness(json, loadImage, decodedImages) {
  for (const material of json.materials) {
    const extension = material.extensions?.KHR_materials_pbrSpecularGlossiness
    if (extension === undefined) continue
    if (!extension || Array.isArray(extension) || typeof extension !== 'object')
      throw Error('Invalid specular-glossiness material')
    const factor = (value) => {
      if (!Number.isFinite(value) || value < 0 || value > 1) throw Error('Invalid specular-glossiness factor')
      return value
    }
    const color = (value, size) => {
      if (value === undefined) return Array(size).fill(1)
      if (!Array.isArray(value) || value.length !== size) throw Error('Invalid specular-glossiness color factor')
      return value.map(factor)
    }
    const diffuse = color(extension.diffuseFactor, 4),
      specular = color(extension.specularFactor, 3),
      gloss = factor(extension.glossinessFactor === undefined ? 1 : extension.glossinessFactor),
      diffuseMap = extension.diffuseTexture,
      specularMap = extension.specularGlossinessTexture
    for (const map of [diffuseMap, specularMap]) {
      if (map === undefined) continue
      if (!map || !Number.isInteger(map.index) || map.index < 0 || !json.textures?.[map.index] ||
        ![0, 1].includes(map.texCoord ?? 0)) throw Error('Invalid specular-glossiness texture')
    }
    const sampler = (map) => json.textures[map.index].sampler
    if (diffuseMap && specularMap &&
      ((diffuseMap.texCoord ?? 0) !== (specularMap.texCoord ?? 0) || sampler(diffuseMap) !== sampler(specularMap) ||
        JSON.stringify(diffuseMap.extensions?.KHR_texture_transform) !==
          JSON.stringify(specularMap.extensions?.KHR_texture_transform)))
      throw Error('Specular-glossiness conversion requires matching texture coordinates and samplers')
    // Khronos' archived workflow conversion assumes 4% dielectric reflectance.
    const brightness = ([r, g, b]) => Math.sqrt(.299 * r * r + .587 * g * g + .114 * b * b)
    const convert = (dc, sc) => {
      const f0 = .04, oneMinusSpecular = 1 - Math.max(...sc), ss = brightness(sc)
      const b = brightness(dc) * oneMinusSpecular / (1 - f0) + ss - 2 * f0
      const metallic = ss < f0 ? 0 :
        clampUnit((-b + Math.sqrt(Math.max(0, b * b - 4 * f0 * (f0 - ss)))) / (2 * f0))
      const weight = metallic * metallic
      const diffuseScale = oneMinusSpecular / ((1 - f0) * Math.max(1e-6, 1 - metallic))
      return [
        dc.map((channel, k) => clampUnit(
          channel * diffuseScale * (1 - weight) + (sc[k] - f0 * (1 - metallic)) * weight / Math.max(1e-6, metallic),
        )),
        metallic,
      ]
    }
    if (!diffuseMap && !specularMap) {
      const [base, metallicFactor] = convert(diffuse.slice(0, 3), specular)
      material.pbrMetallicRoughness = {
        baseColorFactor: [...base, diffuse[3]], metallicFactor, roughnessFactor: 1 - gloss,
      }
      continue
    }
    const d = diffuseMap ? await loadImage(json.textures[diffuseMap.index].source) : null
    const s = specularMap ? await loadImage(json.textures[specularMap.index].source) : null
    const width = Math.min(2048, Math.max(d?.width || 1, s?.width || 1))
    const height = Math.min(2048, Math.max(d?.height || 1, s?.height || 1))
    const base = new Uint8Array(width * height * 4), mr = new Uint8Array(base.length)
    const sample = (img, x, y) => {
      if (!img) return [1, 1, 1, 1]
      const offset = (Math.floor(y * img.height / height) * img.width + Math.floor(x * img.width / width)) * 4
      const pixels = img.pixels
      return [
        srgbLinear(pixels[offset] / 255), srgbLinear(pixels[offset + 1] / 255),
        srgbLinear(pixels[offset + 2] / 255), pixels[offset + 3] / 255,
      ]
    }
    for (let y = 0; y < height; y++)
      for (let x = 0; x < width; x++) {
        const dc = sample(d, x, y), sc = sample(s, x, y)
        const [result, metallic] = convert(
          diffuse.slice(0, 3).map((factor, i) => factor * dc[i]), specular.map((factor, i) => factor * sc[i]),
        )
        const offset = (y * width + x) * 4
        for (let k = 0; k < 3; k++) base[offset + k] = Math.round(linearSrgb(result[k]) * 255)
        base[offset + 3] = Math.round(dc[3] * diffuse[3] * 255)
        mr[offset + 1] = Math.round((1 - sc[3] * gloss) * 255)
        mr[offset + 2] = Math.round(metallic * 255)
        mr[offset + 3] = 255
      }
    const sharedMap = diffuseMap ?? specularMap
    const samplerIndex = sampler(sharedMap)
    const addMap = (pixels) => {
      const source = json.images.length
      json.images.push({})
      decodedImages.set(source, { width, height, pixels })
      const index = json.textures.length
      json.textures.push({ source, sampler: samplerIndex })
      return { ...sharedMap, index }
    }
    material.pbrMetallicRoughness = { baseColorTexture: addMap(base), metallicRoughnessTexture: addMap(mr) }
  }
}
export async function loadGLB(url, { materialVariant } = {}) {
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
          'KHR_materials_volume',
          'KHR_materials_variants',
          'KHR_materials_ior',
          'KHR_materials_specular',
          'KHR_materials_pbrSpecularGlossiness',
          'KHR_materials_sheen',
          'KHR_materials_iridescence',
          'KHR_materials_emissive_strength',
          'KHR_xmp_json_ld',
          'KHR_xmp',
          'EXT_mesh_gpu_instancing',
          'KHR_materials_clearcoat',
          'KHR_materials_unlit',
          'KHR_texture_transform',
        ].includes(e),
    ) ||
    json.buffers.length !== 1 ||
    json.buffers[0].uri
  )
    throw Error('Expected embedded GLB data and supported glTF extensions')
  json.materials ??= []
  const decodedImages = new Map()
  const loadImage = async (source) => {
    if (!decodedImages.has(source)) {
      const image = json.images[source]
      if (image.bufferView === undefined) throw Error('GLB images must be embedded')
      const view = json.bufferViews[image.bufferView],
        offset = view.byteOffset || 0
      if (offset + view.byteLength > bin.byteLength) throw Error('Image out of bounds')
      const decoded = await decodeMaterialImage(
        new Blob([bin.slice(offset, offset + view.byteLength)], { type: image.mimeType }),
      )
      decodedImages.set(source, decoded)
    }
    return decodedImages.get(source)
  }
  await convertSpecularGlossiness(json, loadImage, decodedImages)
  const variants = json.extensions?.KHR_materials_variants?.variants ?? []
  if (materialVariant !== undefined &&
    (!Number.isInteger(materialVariant) || materialVariant < 0 || materialVariant >= variants.length))
    throw Error('Unknown glTF material variant')
  for (const mesh of json.meshes || [])
    for (const primitive of mesh.primitives || []) {
      const extension = primitive.extensions?.KHR_materials_variants
      if (!extension) continue
      if (!Array.isArray(extension.mappings)) throw Error('Invalid glTF material variant mappings')
      const used = new Set()
      for (const {material, variants: indices} of extension.mappings) {
        if (!Number.isInteger(material) || material < 0 || material >= json.materials.length || !Array.isArray(indices))
          throw Error('Invalid glTF material variant mapping')
        for (const index of indices) {
          if (!Number.isInteger(index) || index < 0 || index >= variants.length || used.has(index))
            throw Error('Invalid or duplicate glTF material variant index')
          used.add(index)
          if (index === materialVariant) primitive.material = material
        }
      }
    }
  const defaultMaterial = json.materials.length
  const raw = new DataView(bin)
  function bufferElement(viewIndex, offset, row, stride, size) {
    const view = json.bufferViews[viewIndex]
    const start = (view.byteOffset ?? 0) + offset + row * stride
    if (view.buffer !== 0 || stride < size || offset < 0 || start < 0
      || offset + row * stride + size > view.byteLength || start + size > bin.byteLength)
      throw Error('Out-of-bounds glTF accessor')
    return start
  }
  function element(index, i, type, components, color = false, rotation = false) {
    const a = json.accessors[index]
    const size = { 5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4 }[a.componentType]
    const uvDivisor =
      (type === 'VEC2' || color) && a.normalized ? { 5121: 255, 5123: 65535 }[a.componentType] : 0
    const signedDivisor = rotation && a.normalized ? {5120:127, 5122:32767}[a.componentType] : 0
    if (
      (a.normalized && !uvDivisor && !signedDivisor) ||
      (rotation && a.componentType !== 5126 && !signedDivisor) ||
      ((type === 'VEC2' || color) && a.componentType !== 5126 && !uvDivisor) ||
      a.type !== type || i >= a.count || !size
    )
      throw Error('Unsupported glTF accessor')
    const elementSize = size * components
    let start
    if (a.sparse) {
      const { count, indices, values } = a.sparse
      const width = { 5121: 1, 5123: 2, 5125: 4 }[indices.componentType]
      if (!width || !Number.isInteger(count) || count <= 0 || count > a.count)
        throw Error('Invalid sparse glTF accessor')
      let first = 0, last = count
      while (first < last) {
        const row = Math.floor((first + last) / 2)
        const offset = bufferElement(indices.bufferView, indices.byteOffset ?? 0, row, width, width)
        const vertex = width === 1 ? raw.getUint8(offset)
          : width === 2 ? raw.getUint16(offset, true) : raw.getUint32(offset, true)
        if (vertex >= a.count) throw Error('Invalid sparse glTF index')
        if (vertex === i) {
          start = bufferElement(values.bufferView, values.byteOffset ?? 0, row, elementSize, elementSize)
          break
        }
        if (vertex < i) first = row + 1
        else last = row
      }
    }
    if (start === undefined) {
      if (a.bufferView === undefined) {
        if (!a.sparse) throw Error('Missing glTF buffer view')
        return Array(components).fill(0)
      }
      const stride = json.bufferViews[a.bufferView].byteStride || elementSize
      start = bufferElement(a.bufferView, a.byteOffset ?? 0, i, stride, elementSize)
    }
    return Array.from({ length: components }, (_, k) =>
      signedDivisor
        ? Math.max(-1, (size === 1 ? raw.getInt8(start + k) : raw.getInt16(start + k * size, true)) / signedDivisor)
        : a.componentType === 5126
        ? raw.getFloat32(start + k * size, true)
        : a.componentType === 5125
          ? raw.getUint32(start + k * size, true)
          : a.componentType === 5123
            ? raw.getUint16(start + k * size, true) / (uvDivisor || 1)
            : raw.getUint8(start + k) / (uvDivisor || 1),
    )
  }
  const instanceExtension = 'EXT_mesh_gpu_instancing'
  const originalNodeCount = json.nodes.length
  for (let i = 0; i < originalNodeCount; i++) {
    const node = json.nodes[i], extension = node.extensions?.[instanceExtension]
    if (!extension) continue
    const mesh = json.meshes?.[node.mesh], attributes = extension.attributes
    if (!mesh || node.skin !== undefined || !attributes ||
      Array.isArray(attributes) || !Object.keys(attributes).length)
      throw Error('Invalid glTF mesh instances')
    let count = 0
    for (const value of Object.values(attributes)) {
      const rows = json.accessors[value]?.count
      if (!Number.isInteger(value) || !Number.isSafeInteger(rows) || rows <= 0 || (count && rows !== count))
        throw Error('Mismatched or invalid glTF instance accessors')
      count = rows
    }
    const vertexCount = mesh.primitives.reduce((sum, p) => sum + json.accessors[p.attributes.POSITION].count, 0)
    if (!vertexCount || count > 10000000 / vertexCount) throw Error('Oversized glTF mesh instances')
    node.children ??= []
    for (let row = 0; row < count; row++) {
      const instance = {mesh: node.mesh, weights: node.weights}
      for (const key of ['TRANSLATION', 'ROTATION', 'SCALE']) {
        const index = attributes[key]
        if (index === undefined) continue
        if (key === 'ROTATION') {
          const rotation = element(index, row, 'VEC4', 4, false, true)
          if (!rotation.every(Number.isFinite)) throw Error('Invalid instance rotation')
          instance.rotation = normalize(rotation)
        } else {
          if (json.accessors[index].componentType !== 5126) throw Error('Invalid glTF instance vector')
          const vector = element(index, row, 'VEC3', 3)
          if (!vector.every(Number.isFinite)) throw Error('Invalid glTF instance vector')
          instance[key.toLowerCase()] = vector
        }
      }
      node.children.push(json.nodes.push(instance) - 1)
    }
    delete node.mesh
    delete node.extensions[instanceExtension]
  }
  const animation = readAnimation(json, bin)
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
  let hasTransmission = false
  const independentUVTransforms = new Map(), independentUVMaps = new Map(), occlusionStreams = new Map()
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
        if (!Number.isInteger(mode) || mode < 0 || mode > 6)
          throw Error('Unsupported glTF primitive mode')
        if (p.material !== undefined && (!Number.isInteger(p.material) || p.material < 0 || p.material >= defaultMaterial))
          throw Error('Invalid glTF material index')
        const materialIndex = p.material ?? defaultMaterial
        if (p.material === undefined && json.materials.length === defaultMaterial)
          json.materials.push({})
        const mat = json.materials[materialIndex],
          pbr = mat.pbrMetallicRoughness || {}
        const extensions = mat.extensions || {}
        const coordinates = [null, null]
        function coordinatesFor(texture) {
          const transform = texture.extensions?.KHR_texture_transform ?? {}
          const source = transform.texCoord ?? texture.texCoord ?? 0
          const offset = transform.offset ?? [0, 0], scale = transform.scale ?? [1, 1]
          const angle = transform.rotation ?? 0
          if (!Number.isInteger(source) || source < 0 || !Array.isArray(offset) ||
            !Array.isArray(scale) || offset.length !== 2 || scale.length !== 2 ||
            ![...offset, ...scale, angle].every(Number.isFinite))
            throw Error('Invalid texture transform')
          return [source, ...offset, ...scale, angle]
        }
        const differs = (a, b) => a.some((v, i) => v !== b[i])
        function addTexture(texture, stream = 0) {
          if (!texture) return
          const next = coordinatesFor(texture)
          if (![0, 1].includes(stream) || (coordinates[stream] && differs(coordinates[stream], next)))
            throw Error('Material requires independent texture coordinate streams')
          coordinates[stream] = next
        }
        addTexture(pbr.baseColorTexture)
        function addIndependentTexture(texture, mask) {
          if (!texture) return
          const target = coordinatesFor(texture), base = coordinates[0]
          if (!base || !differs(base, target)) addTexture(texture)
          else {
            const affine = ([, x, y, sx, sy, angle]) => [
              Math.cos(angle) * sx, Math.sin(angle) * sy, x,
              -Math.sin(angle) * sx, Math.cos(angle) * sy, y,
            ]
            const [a, b, x, c, d, y] = affine(base), [e, f, u, g, h, v] = affine(target)
            const det = a * d - b * c
            if (base[0] !== target[0] || Math.abs(det) < 1e-12)
              throw Error('Independent texture mapping requires a shared invertible UV set')
            const p = (e * d - f * c) / det, q = (f * a - e * b) / det,
              r = (g * d - h * c) / det, t = (h * a - g * b) / det
            const mapping = [p, q, u - p * x - q * y, r, t, v - r * x - t * y]
            const previous = independentUVTransforms.get(materialIndex)
            if (previous && differs(previous, mapping))
              throw Error('Material requires more than one independent texture transform')
            independentUVTransforms.set(materialIndex, mapping)
            independentUVMaps.set(materialIndex, (independentUVMaps.get(materialIndex) ?? 0) | mask)
          }
        }
        addIndependentTexture(pbr.metallicRoughnessTexture, 2)
        addIndependentTexture(mat.normalTexture, 1)
        addTexture(mat.emissiveTexture, mat.emissiveTexture?.texCoord === 1 &&
          (pbr.baseColorTexture || pbr.metallicRoughnessTexture || mat.normalTexture) ? 1 : 0)
        addTexture(extensions.KHR_materials_transmission?.transmissionTexture)
        addTexture(extensions.KHR_materials_volume?.thicknessTexture)
        for (const texture of [
          extensions.KHR_materials_specular?.specularTexture,
          extensions.KHR_materials_specular?.specularColorTexture,
          extensions.KHR_materials_sheen?.sheenColorTexture,
          extensions.KHR_materials_sheen?.sheenRoughnessTexture,
          extensions.KHR_materials_iridescence?.iridescenceTexture,
          extensions.KHR_materials_iridescence?.iridescenceThicknessTexture,
        ]) addTexture(texture, texture?.texCoord ?? 0)
        for (const key of coatTextureKeys) {
          const texture = extensions.KHR_materials_clearcoat?.[key]
          addTexture(texture, texture?.texCoord)
        }
        if (mat.occlusionTexture) {
          const target = coordinatesFor(mat.occlusionTexture), source = mat.occlusionTexture.texCoord ?? 0
          const stream = source === 0 && coordinates[0] && differs(coordinates[0], target) ? 1 : source
          addTexture(mat.occlusionTexture, stream)
          occlusionStreams.set(materialIndex, stream)
        }
        const coordinateReaders = coordinates.map((coordinates, stream) => {
          const [set, ox, oy, sx, sy, angle] = coordinates ?? [stream, 0, 0, 1, 1, 0]
          const accessor = p.attributes['TEXCOORD_' + set]
          if (coordinates && accessor === undefined) throw Error('Textured material is missing its texture coordinate set')
          if (accessor === undefined) return null
          const sin = Math.sin(angle), cos = Math.cos(angle)
          return (vertex) => {
            const [u, v] = element(accessor, vertex, 'VEC2', 2)
            return [ox + cos * sx * u + sin * sy * v, oy - sin * sx * u + cos * sy * v]
          }
        })
        const transmission = extensions.KHR_materials_transmission?.transmissionFactor ?? 0,
          ior = extensions.KHR_materials_ior?.ior ?? 1.5
        if (
          !Number.isFinite(transmission) ||
          transmission < 0 ||
          transmission > 1 ||
          !Number.isFinite(ior) ||
          ior < 1 ||
          ior > 3
        )
          throw Error('Dielectric requires transmission 0..1 and IOR 1..3')
        hasTransmission ||= transmission > 0
        if (
          p.indices !== undefined &&
          ![5121, 5123, 5125].includes(json.accessors[p.indices].componentType)
        )
          throw Error('Invalid index format')
        const inputCount = json.accessors[p.indices ?? p.attributes.POSITION].count
        const pointList = mode === 0, line = mode >= 1 && mode <= 3
        if (!Number.isSafeInteger(inputCount) || inputCount < (pointList ? 1 : line ? 2 : 3)
          || (mode === 4 && inputCount % 3) || (mode === 1 && inputCount % 2))
          throw Error('Invalid primitive element count')
        const primitiveCount = pointList || mode === 2 ? inputCount
          : mode === 1 ? inputCount / 2 : line ? inputCount - 1
          : mode === 4 ? inputCount / 3 : inputCount - 2
        const stride = pointList || line ? 6 : 3
        const count = primitiveCount * stride
        if (vertices.length / modelVertexFloats + count > 10000000)
          throw Error('Oversized model mesh')
        const volumeScale = extensions.KHR_materials_volume ? Math.cbrt(Math.abs(det)) : 1
        const lineEndpoint = [0, 1, 0, 0, 1, 1]
        function vertexIndex(i) {
          const primitive = Math.floor(i / stride), corner = i % stride
          if (pointList) i = primitive
          else if (line)
            i = mode === 1 ? primitive * 2 + lineEndpoint[corner]
              : (primitive + lineEndpoint[corner]) % inputCount
          else if (mode === 5)
            i = primitive + (primitive % 2 && corner < 2 ? 1 - corner : corner)
          else if (mode === 6)
            i = corner === 0 ? 0 : primitive + corner
          return p.indices === undefined ? i : element(p.indices, i, 'SCALAR', 1)[0]
        }
        const weights = node.weights ?? json.meshes[node.mesh].weights ?? []
        if (weights.length && weights.length !== (p.targets?.length ?? 0))
          throw Error('Invalid morph weight count')
        if (weights.some(weight => !Number.isFinite(weight))) throw Error('Invalid morph weight')
        function morph(semantic, base, vertex) {
          for (let i = 0; i < weights.length; i++) {
            const accessor = p.targets[i][semantic]
            if (accessor === undefined || weights[i] === 0) continue
            const delta = element(accessor, vertex, 'VEC3', 3)
            for (let k = 0; k < 3; k++) base[k] += weights[i] * delta[k]
          }
          return base
        }
        const positionAt = (vertex) =>
          morph('POSITION', element(p.attributes.POSITION, vertex, 'VEC3', 3), vertex)
        function faceNormal(i) {
          const first = i - (i % 3),
            positions = [0, 1, 2].map((k) =>
              positionAt(vertexIndex(first + k)),
            )
          return normalize(
            cross(
              positions[1].map((v, k) => v - positions[0][k]),
              positions[2].map((v, k) => v - positions[0][k]),
            ),
          )
        }
        for (let i = 0; i < count; i++) {
          if (stride === 3 && mode !== 4 && i % 3 === 0 && p.indices !== undefined) {
            const a = vertexIndex(i), b = vertexIndex(i + 1), c = vertexIndex(i + 2)
            if (a === b || b === c || c === a) {
              i += 2
              continue
            }
          }
          const index = vertexIndex(
              det < 0 && stride === 3 ? i - (i % 3) + (i % 3 === 1 ? 2 : i % 3 === 2 ? 1 : 0) : i,
            ),
            pos = positionAt(index),
            normal =
              p.attributes.NORMAL !== undefined
                ? morph('NORMAL', element(p.attributes.NORMAL, index, 'VEC3', 3), index)
                : stride === 6 ? [0, 0, 1] : faceNormal(i),
            uv = coordinateReaders[0]?.(index) ?? [0, 0],
            uv1 = coordinateReaders[1]?.(index) ?? uv
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
            stride === 6 ? i % stride + 1 : 0,
            stride === 6 ? (line ? 2 : 1) : volumeScale,
            ...readColor(p.attributes.COLOR_0, index),
          )
          if (animation) {
            const delta = (accessor) => accessor === undefined ? [0, 0, 0] : element(accessor, index, 'VEC3', 3)
            const deltas = (p.targets ?? []).map(t => [delta(t.POSITION), delta(t.NORMAL)])
            animation.addVertex(node.skin ?? -1, p, index, m, meshNodeIndex,
              element(p.attributes.POSITION, index, 'VEC3', 3),
              p.attributes.NORMAL === undefined ? normal : element(p.attributes.NORMAL, index, 'VEC3', 3), deltas)
          }
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
  if (json.materials.length > materialSlotLimit) throw Error('At most 512 material slots are supported')
  const materials = []
  for (const [materialIndex, material] of json.materials.entries()) {
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
    const sheen = material.extensions?.KHR_materials_sheen
    if (sheen?.sheenColorFactor !== undefined && !Array.isArray(sheen.sheenColorFactor))
      throw Error('Invalid glTF sheen color factor')
    if (sheen?.sheenRoughnessFactor !== undefined && typeof sheen.sheenRoughnessFactor !== 'number')
      throw Error('Invalid glTF sheen roughness factor')
    const film = material.extensions?.KHR_materials_iridescence
    for (const key of ['iridescenceFactor', 'iridescenceIor', 'iridescenceThicknessMinimum', 'iridescenceThicknessMaximum'])
      if (film?.[key] !== undefined && typeof film[key] !== 'number') throw Error('Invalid glTF ' + key)
    const volume = material.extensions?.KHR_materials_volume
    const distance = volume?.attenuationDistance
    if (distance !== undefined && (!Number.isFinite(distance) || distance <= 0))
      throw Error('Attenuation distance must be positive')
    const textures = [
      pbr.baseColorTexture,
      material.normalTexture,
      pbr.metallicRoughnessTexture,
      material.occlusionTexture,
      material.emissiveTexture,
      ...coatTextureKeys.map((key) => coat?.[key]),
      material.extensions?.KHR_materials_transmission?.transmissionTexture,
      volume?.thicknessTexture,
      specular?.specularTexture,
      specular?.specularColorTexture,
      sheen?.sheenColorTexture,
      sheen?.sheenRoughnessTexture,
      film?.iridescenceTexture,
      film?.iridescenceThicknessTexture,
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
      [255, 255, 255, 255],
      [255, 255, 255, 255],
      [255, 255, 255, 255],
      [255, 255, 255, 255],
      [255, 255, 255, 255],
      [255, 255, 255, 255],
    ]
    const baseSampler = json.samplers?.[json.textures?.[pbr.baseColorTexture?.index]?.sampler]
    const wrap = (mode) => mode === 33071 ? 1 : mode === 33648 ? 2 : 0
    const baseColorWrap = wrap(baseSampler?.wrapS) | (wrap(baseSampler?.wrapT) << 2)
    const images = []
    try {
      for (let i = 0; i < textures.length; i++) {
        if (!textures[i]) {
          images.push({ width: 1, height: 1, pixels: new Uint8Array(fallback[i]) })
          continue
        }
        images.push(await loadImage(json.textures[textures[i].index].source))
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
        unlit: material.extensions?.KHR_materials_unlit !== undefined,
        baseColorWrap,
        emissiveTexCoord: material.emissiveTexture?.texCoord === 1 &&
          (pbr.baseColorTexture || pbr.metallicRoughnessTexture || material.normalTexture) ? 1 : 0,
          normalScale: material.normalTexture?.scale ?? 1,
          occlusionStrength: material.occlusionTexture?.strength ?? 1,
          occlusionTexCoord: occlusionStreams.get(materialIndex) ?? material.occlusionTexture?.texCoord ?? 0,
          independentUVTransform: independentUVTransforms.get(materialIndex),
          independentUVMaps: independentUVMaps.get(materialIndex),
          emissive: (material.emissiveFactor || [0, 0, 0]).map((v) => v * emissiveStrength),
          baseColorTexture: textures[0] ? images[0] : null,
          normalTexture: textures[1] ? images[1] : null,
          metallicRoughnessTexture: textures[2] ? images[2] : null,
          occlusionTexture: textures[3] ? images[3] : null,
          emissiveTexture: textures[4] ? images[4] : null,
          clearcoatTexture: textures[5] ? images[5] : null,
          clearcoatRoughnessTexture: textures[6] ? images[6] : null,
          clearcoatNormalTexture: textures[7] ? images[7] : null,
          transmissionTexture: textures[8] ? images[8] : null,
          thicknessTexture: textures[9] ? images[9] : null,
          specularTexture: textures[10] ? images[10] : null,
          specularColorTexture: textures[11] ? images[11] : null,
          specularTexCoords: [specular?.specularTexture?.texCoord ?? 0, specular?.specularColorTexture?.texCoord ?? 0],
          sheenColorFactor: sheen?.sheenColorFactor,
          sheenRoughnessFactor: sheen?.sheenRoughnessFactor,
          sheenColorTexture: textures[12] ? images[12] : null,
          sheenRoughnessTexture: textures[13] ? images[13] : null,
          iridescenceFactor: film?.iridescenceFactor,
          iridescenceBaseIor: material.extensions?.KHR_materials_ior?.ior,
          iridescenceIor: film?.iridescenceIor,
          iridescenceThicknessMinimum: film?.iridescenceThicknessMinimum,
          iridescenceThicknessMaximum: film?.iridescenceThicknessMaximum,
          iridescenceTexture: textures[14] ? images[14] : null,
          iridescenceThicknessTexture: textures[15] ? images[15] : null,
          iridescenceTexCoords: [film?.iridescenceTexture?.texCoord ?? 0, film?.iridescenceThicknessTexture?.texCoord ?? 0],
          sheenTexCoords: [sheen?.sheenColorTexture?.texCoord ?? 0, sheen?.sheenRoughnessTexture?.texCoord ?? 0],
          thicknessFactor: volume?.thicknessFactor,
          attenuationColor: volume?.attenuationColor,
          inverseAttenuationDistance: distance === undefined ? 0 : 1 / distance,
        }),
      )
    } catch (e) {
      for (const im of images) im.bitmap?.close()
      throw e
    }
  }
  return packMaterials({
    animation,
    hasTransmission,
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
