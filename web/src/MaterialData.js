import { materialSlotLimit } from './ModelVertex.js'
import { decodeMaterialImage } from './MaterialImage.js'
export const materialTextureKeys = [
  'baseColorTexture',
  'normalTexture',
  'metallicRoughnessTexture',
  'occlusionTexture',
  'emissiveTexture',
  'clearcoatTexture',
  'clearcoatRoughnessTexture',
  'clearcoatNormalTexture',
]
const defaults = [
  [255, 255, 255, 255],
  [128, 128, 255, 255],
  [255, 255, 255, 255],
  [255, 255, 255, 255],
  [255, 255, 255, 255],
  [255, 255, 255, 255],
  [255, 255, 255, 255],
  [128, 128, 255, 255],
].map((pixels) => ({ width: 1, height: 1, pixels: new Uint8Array(pixels) }))
export async function loadMaterialImage(url) {
  const response = await fetch(url)
  if (!response.ok) throw Error('Cannot load material image: ' + url)
  return decodeMaterialImage(await response.blob())
}

export function validateMaterial(m) {
  if (!m || typeof m !== 'object') throw Error('Invalid material')
  const out = {
    name: m.name ?? '',
    alphaMode: m.alphaMode ?? 'OPAQUE',
    alphaCutoff: m.alphaCutoff ?? 0.5,
    doubleSided: m.doubleSided ?? false,
    unlit: m.unlit ?? false,
    emissiveTexCoord: m.emissiveTexCoord ?? 0,
    baseColorWrap: m.baseColorWrap ?? 0,
    thicknessFactor: m.thicknessFactor ?? 0,
    inverseAttenuationDistance: m.inverseAttenuationDistance ?? 0,
    attenuationColor: Array.from(m.attenuationColor ?? [1, 1, 1]),
    baseColor: m.baseColor ?? [1, 1, 1, 1],
    roughness: m.roughness ?? 1,
    metallic: m.metallic ?? 1,
    normalScale: m.normalScale ?? 1,
    independentUVTransform: m.independentUVTransform ? Array.from(m.independentUVTransform) : null,
    independentUVMaps: m.independentUVMaps ?? 0,
    occlusionStrength: m.occlusionStrength ?? 1,
    occlusionTexCoord: m.occlusionTexCoord ?? 0,
    emissive: m.emissive ?? [0, 0, 0],
    specularFactor: m.specularFactor ?? 1,
    clearcoatFactor: m.clearcoatFactor ?? 0,
    clearcoatRoughnessFactor: m.clearcoatRoughnessFactor ?? 0,
    clearcoatNormalScale: m.clearcoatNormalScale ?? 1,
    clearcoatTexCoords: Array.from(m.clearcoatTexCoords ?? [0, 0, 0]),
    specularColorFactor: m.specularColorFactor ?? [1, 1, 1],
    specularTexCoords: Array.from(m.specularTexCoords ?? [0, 0]),
    sheenColorFactor: Array.from(m.sheenColorFactor ?? [0, 0, 0]),
    sheenRoughnessFactor: m.sheenRoughnessFactor ?? 0,
    sheenTexCoords: Array.from(m.sheenTexCoords ?? [0, 0]),
    iridescenceFactor: m.iridescenceFactor ?? 0,
    iridescenceIor: m.iridescenceIor ?? 1.3,
    iridescenceBaseIor: m.iridescenceBaseIor ?? 1.5,
    iridescenceThicknessMinimum: m.iridescenceThicknessMinimum ?? 100,
    iridescenceThicknessMaximum: m.iridescenceThicknessMaximum ?? 400,
    iridescenceTexCoords: Array.from(m.iridescenceTexCoords ?? [0, 0]),
    anisotropyStrength: m.anisotropyStrength ?? 0,
    anisotropyRotation: m.anisotropyRotation ?? 0,
    anisotropyTexCoord: m.anisotropyTexCoord ?? 0,
  }
  if (![0,1,2,4,5,6,8,9,10].includes(out.baseColorWrap)) throw Error("Invalid base-color texture wrapping")
  if (![0, 1].includes(out.emissiveTexCoord)) throw Error('Emissive texture coordinate set must be 0 or 1')
  if (!['OPAQUE', 'MASK', 'BLEND'].includes(out.alphaMode) || typeof out.doubleSided !== 'boolean' || typeof out.unlit !== 'boolean')
    throw Error('Invalid material alpha mode or sidedness')
  if (
    !Number.isFinite(out.clearcoatNormalScale) ||
    !Number.isFinite(Math.fround(out.clearcoatNormalScale))
  )
    throw Error('Clearcoat normal scale must be finite')
  if (
    out.clearcoatTexCoords.length !== 3 ||
    out.clearcoatTexCoords.some((uv) => uv !== 0 && uv !== 1)
  )
    throw Error('Clearcoat coordinate set must be 0 or 1')
  if (out.specularTexCoords.length !== 2 || out.specularTexCoords.some((uv) => uv !== 0 && uv !== 1))
    throw Error('Specular coordinate set must be 0 or 1')
  if (!Number.isFinite(out.iridescenceBaseIor) || out.iridescenceBaseIor < 1 || out.iridescenceBaseIor > 3)
    throw Error('Film substrate index of refraction must be between one and three')
  if (!Number.isFinite(Math.fround(out.iridescenceIor)) || out.iridescenceIor < 1)
    throw Error('Film index of refraction must be at least one')
  if (out.iridescenceTexCoords.length !== 2 || out.iridescenceTexCoords.some((uv) => uv !== 0 && uv !== 1))
    throw Error('Iridescence coordinate set must be 0 or 1')
  if (!Number.isFinite(Math.fround(out.anisotropyRotation)) || ![0, 1].includes(out.anisotropyTexCoord))
    throw Error('Invalid anisotropy rotation or coordinate set')
  if (out.sheenTexCoords.length !== 2 || out.sheenTexCoords.some((uv) => uv !== 0 && uv !== 1))
    throw Error('Sheen coordinate set must be 0 or 1')
  if (out.independentUVTransform && (out.independentUVTransform.length !== 6 ||
    !out.independentUVTransform.every((v) => Number.isFinite(Math.fround(v)))))
    throw Error('Invalid independent texture mapping')
  if (![0, 1, 2, 3].includes(out.independentUVMaps) || (out.independentUVMaps && !out.independentUVTransform))
    throw Error('Invalid independent texture map selection')
  if (![0, 1].includes(out.occlusionTexCoord))
    throw Error('AO texture coordinate set must be 0 or 1')
  if (typeof out.name !== 'string') throw Error('Material name must be a string')
  const range = (x, max) => {
    if (typeof x !== 'number' || !Number.isFinite(x) || x < 0 || x > max)
      throw Error('Invalid material factor')
  }
  range(out.thicknessFactor, 3.402823466e38)
  range(out.inverseAttenuationDistance, 3.402823466e38)
  if (out.attenuationColor.length !== 3) throw Error('Invalid attenuation color')
  for (const value of out.attenuationColor) range(value, 1)
  for (const [key, n, max] of [
    ['baseColor', 4, 1],
    ['emissive', 3, 65504],
    ['specularColorFactor', 3, 3.4028234663852886e38],
    ['sheenColorFactor', 3, 1],
  ]) {
    if (!(Array.isArray(out[key]) || out[key] instanceof Float32Array) || out[key].length !== n)
      throw Error('Invalid material color')
    out[key] = Array.from(out[key], (x) => {
      range(x, max)
      return Math.fround(x)
    })
  }
  for (const [key, max] of [
    ['alphaCutoff', 1],
    ['roughness', 1],
    ['metallic', 1],
    ['specularFactor', 1],
    ['sheenRoughnessFactor', 1],
    ['iridescenceFactor', 1],
    ['anisotropyStrength', 1],
    ['iridescenceThicknessMinimum', 3.4028234663852886e38],
    ['iridescenceThicknessMaximum', 3.4028234663852886e38],
    ['clearcoatFactor', 1],
    ['clearcoatRoughnessFactor', 1],
    ['normalScale', 16],
    ['occlusionStrength', 1],
  ]) {
    range(out[key], max)
    out[key] = Math.fround(out[key])
  }
  for (const key of [...materialTextureKeys, "transmissionTexture", "thicknessTexture", "specularTexture", "specularColorTexture", "sheenColorTexture", "sheenRoughnessTexture", "iridescenceTexture", "iridescenceThicknessTexture", "anisotropyTexture"]) {
    const im = m[key]
    if (im == null) {
      out[key] = null
      continue
    }
    if (
      !Number.isInteger(im.width) ||
      !Number.isInteger(im.height) ||
      im.width < 1 ||
      im.height < 1 ||
      im.width > 8192 ||
      im.height > 8192 ||
      !(im.pixels instanceof Uint8Array || im.pixels instanceof Uint8ClampedArray) ||
      im.pixels.length !== im.width * im.height * 4
    )
      throw Error('Material maps require 1..8192 RGBA8 pixels')
    out[key] = { width: im.width, height: im.height, pixels: new Uint8Array(im.pixels) }
  }
  return out
}
export function packMaterials(data) {
  if (!data.materials?.length) return data
  if (data.materials.length > materialSlotLimit) throw Error('At most 512 material slots are supported')
  const materials = data.materials,
    first = materials[0],
    atlas = materials.length > 1 || !!first.transmissionTexture || !!first.thicknessTexture || !!first.specularTexture || !!first.specularColorTexture || !!first.sheenColorTexture || !!first.sheenRoughnessTexture || !!first.iridescenceTexture || !!first.iridescenceThicknessTexture || !!first.anisotropyTexture
  data.baseColor = [...first.baseColor]
  data.material = [first.metallic, first.roughness, first.normalScale, first.occlusionStrength]
  data.emissive = [...first.emissive, 0]
  const texturedCoat = materials.some(
    (m) => m.clearcoatTexture || m.clearcoatRoughnessTexture || m.clearcoatNormalTexture || m.sheenColorTexture || m.sheenRoughnessTexture || m.iridescenceTexture || m.iridescenceThicknessTexture || m.anisotropyTexture,
  )
  const mapCount = texturedCoat ? materialTextureKeys.length : 5
  let tile = 1
  for (const m of materials)
    for (let k = 0; k < mapCount; k++) {
      const im = m[materialTextureKeys[k]] || defaults[k]
      tile = Math.max(tile, im.width, im.height)
    }
  for (const m of materials)
    for (const image of [m.transmissionTexture, m.thicknessTexture, m.specularTexture, m.specularColorTexture, m.sheenColorTexture, m.sheenRoughnessTexture, m.iridescenceTexture, m.iridescenceThicknessTexture, m.anisotropyTexture])
      if (image) tile = Math.max(tile, image.width, image.height)
  tile = Math.min(tile, 2048)
  const cols = Math.ceil(Math.sqrt(materials.length)),
    rows = Math.ceil(materials.length / cols)
  let stride = tile + 4,
    width = cols * stride,
    height = rows * stride
  while (
    atlas &&
    tile > 1 &&
    (width > 8192 || height > 8192 || width * height * mapCount * 4 > 512 * 1024 * 1024)
  ) {
    tile = Math.max(1, Math.floor(tile / 2))
    stride = tile + 4
    width = cols * stride
    height = rows * stride
  }
  if (atlas && (width > 8192 || height > 8192 || width * height * mapCount * 4 > 512 * 1024 * 1024))
    throw Error('Material atlas exceeds 512 MiB / 8192px limit')
  data.images = atlas
    ? defaults.map((image, k) =>
        k < mapCount ? { width, height, pixels: new Uint8Array(width * height * 4) } : image,
      )
    : materialTextureKeys.map((key, k) => first[key] || defaults[k])
  data.materialRecords = new Float32Array(materials.length * 48)
  materials.forEach((m, i) => {
    const record = data.materialRecords.subarray(i * 48, (i + 1) * 48),
      x = (i % cols) * stride,
      y = Math.floor(i / cols) * stride
    for (const k of [0, 5, 10, 15, 16, 21, 26, 31]) record[k] = 1
    // Material records reuse the first two columns of the unused world matrix.
    record.set(m.specularColorFactor ?? [1, 1, 1], 0)
    record[3] = m.specularFactor ?? 1
    record[4] = m.clearcoatFactor ?? 0
    record[5] = m.clearcoatRoughnessFactor ?? 0
    record[6] = m.occlusionTexCoord ?? 0
    record[7] =
      (m.clearcoatTexture ? 1 : 0) |
      (m.clearcoatRoughnessTexture ? 2 : 0) |
      (m.clearcoatNormalTexture ? 4 : 0)
    const uv = m.clearcoatTexCoords ?? [0, 0, 0]
    record[8] = uv[0] | (uv[1] << 1) | (uv[2] << 2)
    record[9] = m.clearcoatNormalScale ?? 1
    record[10] = (m.specularTexture ? 1 : 0) | (m.specularColorTexture ? 2 : 0) |
      ((m.specularTexCoords?.[0] ?? 0) << 2) | ((m.specularTexCoords?.[1] ?? 0) << 3)
    record.set(m.attenuationColor ?? [1, 1, 1], 12)
    record[15] = (m.thicknessFactor ?? 0) * (m.inverseAttenuationDistance ?? 0)
    record.set(m.sheenColorFactor ?? [0, 0, 0], 16)
    record[19] = m.sheenRoughnessFactor ?? 0
    record[20] = (m.sheenColorTexture ? 1 : 0) | (m.sheenRoughnessTexture ? 2 : 0) |
      ((m.sheenTexCoords?.[0] ?? 0) << 2) | ((m.sheenTexCoords?.[1] ?? 0) << 3) |
      (m.iridescenceTexture ? 16 : 0) | (m.iridescenceThicknessTexture ? 32 : 0) |
      ((m.iridescenceTexCoords?.[0] ?? 0) << 6) | ((m.iridescenceTexCoords?.[1] ?? 0) << 7) |
      (Math.round(((m.iridescenceBaseIor ?? 1.5) - 1) * 2047.5) << 8) |
      (m.anisotropyTexture ? 1 << 20 : 0) | ((m.anisotropyTexCoord ?? 0) << 21)
    record.set([m.iridescenceFactor ?? 0, m.iridescenceIor ?? 1.3,
      m.iridescenceThicknessMinimum ?? 100, m.iridescenceThicknessMaximum ?? 400], 28)
    if (m.independentUVTransform) {
      record.set(m.independentUVTransform, 21)
    }
    const rotation = ((m.anisotropyRotation ?? 0) % Math.PI) / Math.PI
    const turn = Math.round((rotation < 0 ? rotation + 1 : rotation) * 2048) & 2047
    record[27] = (m.independentUVMaps ?? 0) |
      (Math.round((m.anisotropyStrength ?? 0) * 2047) << 2) | (turn << 13)
    record.set(m.baseColor, 32)
    record.set([m.metallic, m.roughness, m.normalScale, m.occlusionStrength], 36)
    record.set(m.emissive, 40)
    record[43] =
      ['OPAQUE', 'MASK', 'BLEND'].indexOf(m.alphaMode ?? 'OPAQUE') +
      (m.doubleSided ? 4 : 0) +
      (m.unlit ? 8 : 0) +
      (m.emissiveTexCoord ? 16 : 0) +
      (m.baseColorWrap ?? 0) * 32 +
      (m.transmissionTexture ? 512 : 0) +
      (m.thicknessTexture ? 1024 : 0) +
      (m.alphaCutoff ?? 0.5) * 0.25
    record.set(
      atlas
        ? [(tile - 1) / width, (tile - 1) / height, (x + 2.5) / width, (y + 2.5) / height]
        : [1, 1, 0, 0],
      44,
    )
    if (atlas)
      for (let k = 0; k < mapCount; k++) {
        const src = m[materialTextureKeys[k]] || defaults[k],
          dst = data.images[k].pixels
        for (let py = 0; py < stride; py++)
          for (let px = 0; px < stride; px++) {
            const sx = Math.round(
                Math.max(0, Math.min(1, (px - 2) / Math.max(1, tile - 1))) * (src.width - 1),
              ),
              sy = Math.round(
                Math.max(0, Math.min(1, (py - 2) / Math.max(1, tile - 1))) * (src.height - 1),
              ),
              from = (sy * src.width + sx) * 4
            const target = ((y + py) * width + x + px) * 4
            dst.set(src.pixels.subarray(from, from + 4), target)
            const channel = (map, component) => {
              if (!map) return 255
              const x = Math.round(Math.max(0, Math.min(1, (px - 2) / Math.max(1, tile - 1))) * (map.width - 1))
              const y = Math.round(Math.max(0, Math.min(1, (py - 2) / Math.max(1, tile - 1))) * (map.height - 1))
              return map.pixels[(y * map.width + x) * 4 + component]
            }
            if (k === 2) {
              dst[target] = channel(m.thicknessTexture, 1)
              dst[target + 3] = channel(m.transmissionTexture, 0)
            }
            if (k === 1 && m.specularTexture)
              dst[target + 3] = channel(m.specularTexture, 3)
            if (k === 6) {
              if (m.sheenRoughnessTexture) dst[target + 3] = channel(m.sheenRoughnessTexture, 3)
              if (m.iridescenceTexture) dst[target] = channel(m.iridescenceTexture, 0)
              if (m.iridescenceThicknessTexture) dst[target + 2] = channel(m.iridescenceThicknessTexture, 1)
            }
            if (k === 7 && m.anisotropyTexture)
              dst[target + 3] = channel(m.anisotropyTexture, 2)
            if (k === 4 && m.anisotropyTexture) {
              const dx = channel(m.anisotropyTexture, 0) * (2 / 255) - 1
              const dy = channel(m.anisotropyTexture, 1) * (2 / 255) - 1
              const turn = Math.atan2(dy, dx) / Math.PI
              dst[target + 3] = Math.round((turn - Math.floor(turn)) * 256) & 255
            }
            const packedColor = k === 3 ? m.specularColorTexture : k === 5 ? m.sheenColorTexture : null
            if (packedColor) {
              for (let component = 0; component < 3; component++) {
                const value = channel(packedColor, component) / 255
                dst[target + component + 1] = Math.round(255 * (value <= .04045 ? value / 12.92 : ((value + .055) / 1.055) ** 2.4))
              }
            }
          }
      }
  })
  return data
}

export function materialCapabilities() {
  return {
    opaque: true,
    mask: true,
    blend: true,
    doubleSided: true,
    maskShadows: true,
    blendShadows: false,
    maskRayTracing: false,
    blendRayTracing: false,
    rayMaterialSidedness: false,
    alphaAwarePicking: false,
    blendMSAA: false,
    blendTAA: false,
    blendAASplit: false,
    transparency: 'weighted-blended-oit',
  }
}
