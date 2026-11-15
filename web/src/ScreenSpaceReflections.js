import { loadEnvironmentFile } from './EnvironmentData.js'
export const defaultScreenSpaceReflections = () => ({
  enabled: true,
  hierarchical: true,
  maxSteps: 128,
  maxDistance: 24,
  thickness: 0.12,
  stride: 1,
  roughnessCutoff: 0.8,
  edgeFade: 0.08,
  intensity: 1,
  environmentIntensity: 1,
  debug: 'composite',
  debugMip: 3,
})
export const reflectionDebugModes = [
  'composite',
  'reflections',
  'hit-mask',
  'normals',
  'roughness',
  'linear-depth',
  'depth-pyramid',
]
export function copyScreenSpaceReflections(value) {
  const s = { ...defaultScreenSpaceReflections(), ...value }
  if (
    typeof s.enabled !== 'boolean' ||
    typeof s.hierarchical !== 'boolean' ||
    !Number.isInteger(s.maxSteps) ||
    s.maxSteps < 16 ||
    s.maxSteps > 256 ||
    !Number.isInteger(s.debugMip) ||
    s.debugMip < 0 ||
    s.debugMip > 12 ||
    !reflectionDebugModes.includes(s.debug)
  )
    throw Error('Invalid reflection settings')
  for (const [key, min, max] of [
    ['maxDistance', 1, 60],
    ['thickness', 0.01, 1],
    ['stride', 0.5, 4],
    ['roughnessCutoff', 0.05, 1],
    ['edgeFade', 0.005, 0.3],
    ['intensity', 0, 2],
    ['environmentIntensity', 0, 4],
  ])
    if (!Number.isFinite(s[key]) || s[key] < min || s[key] > max)
      throw Error('Invalid reflection ' + key)
  return s
}
const f32 = new Float32Array(1),
  bits = new Uint32Array(f32.buffer)
function half(v) {
  f32[0] = v
  const x = bits[0]
  let e = ((x >>> 23) & 255) - 127 + 15,
    m = (x & 0x7fffff) + 0x1000
  if (e <= 0) return 0
  if (e >= 31) return 0x7bff
  if (m & 0x800000) {
    m = 0
    e++
  }
  return ((x >>> 16) & 0x8000) | (e << 10) | (m >>> 13)
}
export function reflectionEnvironmentAtlas(env) {
  const pixels = new Uint16Array(256 * 256 * 4)
  for (let level = 0; level < 9; level++) {
    const w = Math.max(1, 256 >> level),
      h = Math.max(1, 128 >> level),
      offset = 256 - (256 >> level),
      rough = level / 8
    const mip = env?.mips?.[Math.floor(rough * (env.mips.length - 1) + 0.5)],
      src =
        mip && new Uint16Array(mip.pixels.buffer, mip.pixels.byteOffset, mip.pixels.byteLength / 2)
    for (let y = 0; y < h; y++)
      for (let x = 0; x < w; x++) {
        const phi = ((x + 0.5) / w - 0.5) * 2 * Math.PI,
          theta = ((y + 0.5) / h) * Math.PI,
          d = [Math.sin(theta) * Math.cos(phi), Math.cos(theta), Math.sin(theta) * Math.sin(phi)],
          at = ((offset + y) * 256 + x) * 4
        if (mip) {
          const [ax, ay, az] = d.map(Math.abs)
          let face, u, v
          if (ax >= ay && ax >= az) {
            face = d[0] >= 0 ? 0 : 1
            u = (d[0] >= 0 ? -d[2] : d[2]) / ax
            v = -d[1] / ax
          } else if (ay >= az) {
            face = d[1] >= 0 ? 2 : 3
            u = d[0] / ay
            v = (d[1] >= 0 ? d[2] : -d[2]) / ay
          } else {
            face = d[2] >= 0 ? 4 : 5
            u = (d[2] >= 0 ? d[0] : -d[0]) / az
            v = -d[1] / az
          }
          const ix = Math.min(mip.size - 1, Math.floor((u * 0.5 + 0.5) * mip.size)),
            iy = Math.min(mip.size - 1, Math.floor((v * 0.5 + 0.5) * mip.size)),
            p = ((face * mip.size + iy) * mip.size + ix) * 4
          pixels[at] = src[p]
          pixels[at + 1] = src[p + 1]
          pixels[at + 2] = src[p + 2]
        } else {
          const up = Math.max(0, d[1]),
            light =
              Math.max(0, d[0] * 0.3 + d[1] * 0.8 + d[2] * 0.5196152423) **
                (2 ** (8 * (1 - rough))) *
              (1 - rough * 0.75)
          pixels[at] = half(0.08 + 0.32 * up + light * 5)
          pixels[at + 1] = half(0.1 + 0.4 * up + light * 4)
          pixels[at + 2] = half(0.14 + 0.6 * up + light * 3)
        }
        pixels[at + 3] = 0x3c00
      }
  }
  return { width: 256, height: 256, pixels }
}
export { loadEnvironmentFile }
export function reflectionResourceSize(i, w, h, ss) {
  const scale = ss ? 2 : 1
  if (i >= 50 && i < 62) {
    const d = 2 ** (i - 49)
    return [Math.ceil((w * scale) / d), Math.ceil((h * scale) / d)]
  }
  if ((i >= 42 && i <= 63) || i >= 65) return [w * scale, h * scale]
  return [w, h]
}
export function packReflectionUniforms(u, s, w, h, ss) {
  let side = Math.max(w, h) * (ss ? 2 : 1),
    levels = 0
  while (side > 1) {
    side = Math.ceil(side / 2)
    levels++
  }
  u[25] = levels
  u[26] = 1
  u[27] = 1
  u.splice(32, 4, s.enabled ? 1 : 0, s.maxSteps, s.maxDistance, s.thickness)
  u.splice(36, 4, s.stride, s.roughnessCutoff, s.edgeFade, s.hierarchical ? 1 : 0)
  u.splice(40, 4, s.intensity, reflectionDebugModes.indexOf(s.debug), 7605, s.environmentIntensity)
}
export function appendReflectionPasses(passes, u, s, ms, taa, split) {
  const add = (program, target) => {
    const p = { program, target, u: [...u], sources: Array(16).fill(16) }
    passes.push(p)
    return p
  }
  const scene = add(17, 42)
  scene.ms = ms
  scene.sources[3] = 64
  if (s.enabled || s.debug === 'depth-pyramid') {
    let input = 45
    for (let level = 0; level < u[25]; level++) {
      const p = add(18, 50 + level)
      p.sources[0] = input
      p.u[20] = level === 0 ? 1 : 0
      input = 50 + level
    }
  }
  const bind = (p) => {
    p.sources[0] = 42
    p.sources[1] = 44
    p.sources[2] = 45
    p.sources[3] = 64
  }
  if (s.enabled) {
    const trace = add(19, 62)
    bind(trace)
    for (let i = 0; i < u[25]; i++) trace.sources[4 + i] = 50 + i
  }
  const composite = add(20, 63)
  bind(composite)
  composite.u[22] = Math.min(s.debugMip, u[25])
  if (s.debug === 'depth-pyramid')
    composite.sources[4] =
      u[25] === 0 || s.debugMip === 0
        ? 45
        : 50 + Math.min(s.debugMip ? s.debugMip - 1 : 0, u[25] ? u[25] - 1 : 0)
  else if (s.enabled) composite.sources[4] = 62
  const copy = add(1, 0)
  copy.sources[0] = 63
  copy.u.splice(20, 4, 1, 1, 0, 0)
  copy.u[24] = taa ? 0 : 1
  if (taa) {
    const motion = add(1, 1)
    motion.sources[0] = 43
    motion.u.splice(20, 4, 1, 1, 0, 0)
  }
  if (split) {
    const base = add(1, 9)
    base.sources[0] = 63
    base.u.splice(20, 4, 1, 1, 0, 0)
    base.u[24] = 1
  }
}
