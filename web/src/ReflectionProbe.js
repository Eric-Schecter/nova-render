import { half } from './ScreenSpaceReflections.js'
import { appendReflectionPasses } from './ScreenSpaceReflections.js'
export const defaultReflectionProbe = () => ({
  enabled: true,
  boxProjection: true,
  smoothBlending: true,
  roughness: 0.12,
  intensity: 1,
  blendDistance: 0,
  position: [0, 0.6, 0],
  boxMin: [-4, -1, -4],
  boxMax: [4, 3, 4],
  debug: 'composite',
  probes: [],
  debugProbe: 0,
})
export const copyProbe = (value) => {
  const s = { ...defaultReflectionProbe(), ...value }
  if (
    typeof s.enabled !== 'boolean' ||
    typeof s.boxProjection !== 'boolean' ||
    typeof s.smoothBlending !== 'boolean' ||
    !['composite', 'reflections', 'influence', 'cubemap'].includes(s.debug)
  )
    throw Error('Invalid probe settings')
  for (const [key, max] of [
    ['roughness', 1],
    ['intensity', 2],
    ['blendDistance', 2],
  ])
    if (!Number.isFinite(s[key]) || s[key] < 0 || s[key] > max) throw Error('Invalid probe ' + key)
  for (const key of ['position', 'boxMin', 'boxMax']) {
    if (!Array.isArray(s[key]) || s[key].length !== 3 || !s[key].every(Number.isFinite))
      throw Error('Invalid probe ' + key)
    s[key] = [...s[key]]
  }
  if (!Array.isArray(s.probes) || s.probes.length > 4) throw Error('Probe count must be 1..4')
  s.probes = s.probes.map((volume) => copyVolume(volume))
  const volumes = effectiveProbes(s)
  for (const v of volumes) validateVolume(v)
  if (!Number.isInteger(s.debugProbe) || s.debugProbe < 0 || s.debugProbe >= volumes.length)
    throw Error('Invalid debug probe index')
  return s
}
export function copyVolume(value) {
  const v = {
    enabled: true,
    intensity: 1,
    blendDistance: 0.8,
    position: [0, 0.6, 0],
    boxMin: [-4, -1, -4],
    boxMax: [4, 3, 4],
    influenceMin: [-4, -1, -4],
    influenceMax: [4, 3, 4],
    ...value,
  }
  for (const key of ['position', 'boxMin', 'boxMax', 'influenceMin', 'influenceMax']) {
    if (!Array.isArray(v[key]) || v[key].length !== 3 || !v[key].every(Number.isFinite))
      throw Error('Invalid probe ' + key)
    v[key] = [...v[key]]
  }
  validateVolume(v)
  return v
}
function validateVolume(v) {
  if (
    typeof v.enabled !== 'boolean' ||
    !Number.isFinite(v.intensity) ||
    v.intensity < 0 ||
    v.intensity > 2 ||
    !Number.isFinite(v.blendDistance) ||
    v.blendDistance < 0 ||
    v.blendDistance > 2
  )
    throw Error('Invalid probe volume')
  for (let i = 0; i < 3; i++)
    if (
      v.boxMin[i] < -20 ||
      v.boxMax[i] > 20 ||
      v.boxMax[i] - v.boxMin[i] < 0.25 ||
      v.influenceMin[i] < -20 ||
      v.influenceMax[i] > 20 ||
      v.influenceMax[i] - v.influenceMin[i] < 0.25 ||
      v.position[i] <= v.boxMin[i] + 0.01 ||
      v.position[i] >= v.boxMax[i] - 0.01
    )
      throw Error('Invalid probe box or position')
}
export function effectiveProbes(s) {
  return s.probes.length
    ? s.probes
    : [
        {
          enabled: true,
          intensity: 1,
          position: [...s.position],
          boxMin: [...s.boxMin],
          boxMax: [...s.boxMax],
          influenceMin: [...s.boxMin],
          influenceMax: [...s.boxMax],
          blendDistance: s.blendDistance,
        },
      ]
}
export function probeMetadata(s) {
  const pixels = new Uint16Array(80)
  effectiveProbes(s).forEach((v, i) => {
    const rows = [
      ...v.position,
      v.enabled ? 1 : 0,
      ...v.boxMin,
      v.blendDistance,
      ...v.boxMax,
      v.intensity,
      ...v.influenceMin,
      0,
      ...v.influenceMax,
      0,
    ]
    rows.forEach((value, j) => (pixels[i * 20 + j] = half(value)))
  })
  return { width: 20, height: 1, pixels }
}
export function packProbeUniforms(u, s) {
  u.splice(32, 4, s.enabled ? 1 : 0, -2, s.boxProjection ? 1 : 0, s.roughness)
  u.splice(
    36,
    4,
    s.intensity,
    s.smoothBlending ? 1 : 0,
    ['composite', 'reflections', 'influence', 'cubemap'].indexOf(s.debug),
    effectiveProbes(s).length,
  )
  u[40] = s.debugProbe
  u[41] = 0
  u[42] = 7053 + 108 * effectiveProbes(s).length
  u.splice(44, 3, ...s.position)
  u.splice(48, 3, ...s.boxMin)
  u.splice(52, 3, ...s.boxMax)
}
export function appendProbePasses(passes, u, s, dirty, ms, taa, split) {
  const volumes = effectiveProbes(s)
  volumes.forEach((v, index) => {
    if (dirty[index]) {
      const p = { program: 21, target: 73 + index, u: [...u], sources: Array(16).fill(16) }
      p.u.splice(44, 3, ...v.position)
      p.sources[3] = 64
      passes.push(p)
    }
  })
  appendReflectionPasses(
    passes,
    u,
    { enabled: false, debug: 'composite', debugMip: 0 },
    ms,
    taa,
    split,
  )
  for (const p of passes) {
    if (p.program === 17 || p.program === 20) p.sources[10] = 77
    if (p.program === 20) volumes.forEach((v, i) => (p.sources[6 + i] = 73 + i))
  }
}
