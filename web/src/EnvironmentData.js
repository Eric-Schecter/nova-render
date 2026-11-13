export function defaultEnvironment() {
  return { rotation: 0, intensity: 1, backgroundIntensity: 1, showBackground: true }
}
export function copyEnvironment(value) {
  if (!value || !Number.isFinite(value.rotation) || Math.abs(value.rotation) > 180)
    throw Error('Environment rotation must be in [-180,180] degrees')
  if (
    ![value.intensity, value.backgroundIntensity].every(
      (v) => Number.isFinite(v) && v >= 0 && v <= 16,
    ) ||
    typeof value.showBackground !== 'boolean'
  )
    throw Error('Environment intensities must be in [0,16] and showBackground must be boolean')
  return {
    rotation: value.rotation,
    intensity: value.intensity,
    backgroundIntensity: value.backgroundIntensity,
    showBackground: value.showBackground,
  }
}
export async function loadEnvironmentFile(url) {
  const response = await fetch(url)
  if (!response.ok) throw Error('Cannot load environment: ' + url)
  const bytes = await response.arrayBuffer()
  if (bytes.byteLength < 160 || bytes.byteLength > 128 * 1024 * 1024)
    throw Error('Invalid environment file size')
  const view = new DataView(bytes)
  if (new TextDecoder().decode(new Uint8Array(bytes, 0, 8)) !== 'NOVENV01')
    throw Error('Expected Nova .nenv environment (NOVENV01)')
  const face = view.getUint32(8, true),
    levels = view.getUint32(12, true)
  if (!face || face > 1024 || face & (face - 1) || levels !== Math.log2(face) + 1)
    throw Error('Invalid environment mip chain')
  let expected = 160
  for (let n = face; n >= 1; n /= 2) expected += n * n * 6 * 8
  if (bytes.byteLength !== expected) throw Error('Environment payload size mismatch')
  const irradiance = new Float32Array(36)
  for (let i = 0; i < 36; i++) {
    irradiance[i] = view.getFloat32(16 + i * 4, true)
    if (!Number.isFinite(irradiance[i])) throw Error('Non-finite environment irradiance')
  }
  for (let offset = 160; offset < bytes.byteLength; offset += 2)
    if ((view.getUint16(offset, true) & 0x7c00) === 0x7c00)
      throw Error('Non-finite environment texel')
  const mips = []
  for (let size = face, offset = 160; size >= 1; size /= 2) {
    const length = size * size * 6 * 8
    mips.push({ size, pixels: new Uint8Array(bytes, offset, length) })
    offset += length
  }
  return { irradiance, mips }
}
export const fallbackEnvironment = {
  irradiance: new Float32Array(36),
  mips: [{ size: 1, pixels: new Uint8Array(48) }],
}
