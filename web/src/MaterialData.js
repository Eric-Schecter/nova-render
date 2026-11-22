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
    occlusionStrength: m.occlusionStrength ?? 1,
    occlusionTexCoord: m.occlusionTexCoord ?? 0,
    emissive: m.emissive ?? [0, 0, 0],
    specularFactor: m.specularFactor ?? 1,
    hybridReflectionStrength: m.hybridReflectionStrength ?? null,
    clearcoatFactor: m.clearcoatFactor ?? 0,
    clearcoatRoughnessFactor: m.clearcoatRoughnessFactor ?? 0,
    clearcoatNormalScale: m.clearcoatNormalScale ?? 1,
    clearcoatTexCoords: Array.from(m.clearcoatTexCoords ?? [0, 0, 0]),
    specularColorFactor: m.specularColorFactor ?? [1, 1, 1],
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
    ['clearcoatFactor', 1],
    ['clearcoatRoughnessFactor', 1],
    ['normalScale', 16],
    ['occlusionStrength', 1],
  ]) {
    range(out[key], max)
    out[key] = Math.fround(out[key])
  }
  if (out.hybridReflectionStrength !== null) {
    range(out.hybridReflectionStrength, 1)
    out.hybridReflectionStrength = Math.fround(out.hybridReflectionStrength)
  }
  for (const key of [...materialTextureKeys, "transmissionTexture", "thicknessTexture"]) {
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
  if (data.materials.length > 128) throw Error('At most 128 material slots are supported')
  const materials = data.materials,
    first = materials[0],
    atlas = materials.length > 1 || !!first.transmissionTexture || !!first.thicknessTexture
  data.baseColor = [...first.baseColor]
  data.material = [first.metallic, first.roughness, first.normalScale, first.occlusionStrength]
  data.emissive = [...first.emissive, 0]
  const texturedCoat = materials.some(
    (m) => m.clearcoatTexture || m.clearcoatRoughnessTexture || m.clearcoatNormalTexture,
  )
  const mapCount = texturedCoat ? materialTextureKeys.length : 5
  let tile = 1
  for (const m of materials)
    for (let k = 0; k < mapCount; k++) {
      const im = m[materialTextureKeys[k]] || defaults[k]
      tile = Math.max(tile, im.width, im.height)
    }
  for (const m of materials)
    for (const image of [m.transmissionTexture, m.thicknessTexture])
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
    // Material records reuse the unused world matrix.
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
    record[11] = m.hybridReflectionStrength ?? -1
    record.set(m.attenuationColor ?? [1, 1, 1], 12)
    record[15] = (m.thicknessFactor ?? 0) * (m.inverseAttenuationDistance ?? 0)
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
        const coordinate = (p, size) =>
          Math.round(Math.max(0, Math.min(1, (p - 2) / Math.max(1, tile - 1))) * (size - 1))
        const columns = Int32Array.from(
          { length: stride },
          (_, px) => coordinate(px, src.width) * 4,
        )
        for (let py = 0; py < stride; py++) {
          const sourceRow = coordinate(py, src.height) * src.width * 4
          let to = ((y + py) * width + x) * 4
          for (let px = 0; px < columns.length; px++) {
            const column = columns[px]
            const volumeChannel = (image, channel) => image
              ? image.pixels[(coordinate(py, image.height) * image.width + coordinate(px, image.width)) * 4 + channel]
              : 255
            const from = sourceRow + column
            dst[to++] = k === 2 ? volumeChannel(m.thicknessTexture, 1) : src.pixels[from]
            dst[to++] = src.pixels[from + 1]
            dst[to++] = src.pixels[from + 2]
            dst[to++] = k === 2 ? volumeChannel(m.transmissionTexture, 0) : src.pixels[from + 3]
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
