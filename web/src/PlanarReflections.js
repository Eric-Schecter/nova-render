import { appendReflectionPasses } from './ScreenSpaceReflections.js'
export const defaultPlanarReflections = () => ({
  floorEnabled: true,
  mirrorEnabled: true,
  floorRoughness: 0.06,
  mirrorRoughness: 0.02,
  floorIntensity: 1,
  mirrorIntensity: 1,
})
export function copyPlanarReflections(value) {
  const s = { ...defaultPlanarReflections(), ...value }
  if (typeof s.floorEnabled !== 'boolean' || typeof s.mirrorEnabled !== 'boolean')
    throw Error('Invalid planar toggle')
  for (const key of ['floorRoughness', 'mirrorRoughness', 'floorIntensity', 'mirrorIntensity'])
    if (!Number.isFinite(s[key]) || s[key] < 0 || s[key] > (key.endsWith('Roughness') ? 1 : 2))
      throw Error('Invalid planar ' + key)
  return s
}
export function packPlanarUniforms(u, s) {
  u.splice(32, 4, 0, -1, s.floorEnabled ? 1 : 0, s.mirrorEnabled ? 1 : 0)
  u.splice(36, 4, s.floorRoughness, s.mirrorRoughness, s.floorIntensity, s.mirrorIntensity)
  u[41] = 0
  u[42] = 7611
}
export function appendPlanarPasses(passes, u, s, ms, taa, split) {
  for (let plane = 1; plane <= 2; plane++) {
    if (plane === 1 ? !s.floorEnabled : !s.mirrorEnabled) continue
    const p = { program: 17, target: plane === 1 ? 65 : 69, u: [...u], sources: Array(16).fill(16) }
    p.u[23] = plane
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
  for (const p of passes) {
    if (p.program === 1 && p.target === 1) {
      p.program = 20
      p.u[21] = -1
      p.sources[0] = 42
      p.sources[1] = 44
      p.sources[2] = 45
      p.sources[3] = 64
    }
    if (p.program === 20) {
      p.sources[4] = s.floorEnabled ? 65 : 16
      p.sources[5] = s.mirrorEnabled ? 69 : 16
      p.sources[6] = 43
      p.sources[7] = s.floorEnabled ? 66 : 16
      p.sources[8] = s.mirrorEnabled ? 70 : 16
    }
  }
}
