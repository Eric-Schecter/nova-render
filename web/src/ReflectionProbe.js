import { appendReflectionPasses } from './ScreenSpaceReflections.js'
export const defaultReflectionProbe = () => ({
  enabled: true,
  boxProjection: true,
  roughness: 0.12,
  intensity: 1,
  blendDistance: 0,
  position: [0, 0.6, 0],
  boxMin: [-4, -1, -4],
  boxMax: [4, 3, 4],
  debug: 'composite',
})
export const copyProbe = (value) => {
  const s = { ...defaultReflectionProbe(), ...value }
  if (
    typeof s.enabled !== 'boolean' ||
    typeof s.boxProjection !== 'boolean' ||
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
  for (let i = 0; i < 3; i++)
    if (
      s.boxMin[i] < -20 ||
      s.boxMax[i] > 20 ||
      s.boxMax[i] - s.boxMin[i] < 0.25 ||
      s.position[i] <= s.boxMin[i] + 0.01 ||
      s.position[i] >= s.boxMax[i] - 0.01
    )
      throw Error('Probe must lie inside finite box')
  return s
}
export function packProbeUniforms(u, s) {
  u.splice(32, 4, s.enabled ? 1 : 0, -2, s.boxProjection ? 1 : 0, s.roughness)
  u.splice(
    36,
    4,
    s.intensity,
    s.blendDistance,
    ['composite', 'reflections', 'influence', 'cubemap'].indexOf(s.debug),
    0,
  )
  u[41] = 0
  u[42] = 7161
  u.splice(44, 3, ...s.position)
  u.splice(48, 3, ...s.boxMin)
  u.splice(52, 3, ...s.boxMax)
}
export function appendProbePasses(passes, u, dirty, ms, taa, split) {
  if (dirty) {
    const p = { program: 21, target: 73, u: [...u], sources: Array(16).fill(16) }
    p.sources[3] = 64
    passes.push(p)
  }
  appendReflectionPasses(
    passes,
    u,
    { enabled: false, debug: 'composite', debugMip: 0 },
    ms,
    taa,
    split,
  )
  for (const p of passes) if (p.program === 20) p.sources[6] = 73
}
