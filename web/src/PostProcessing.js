const toneMappings = ['linear', 'reinhard', 'aces', 'agx', 'aces-filmic']
export const defaultPostProcessing = () => ({
  enabled: false,
  toneMapping: 'aces',
  exposureEV: 0,
  autoExposure: false,
  minEV: -6,
  maxEV: 6,
  adaptationRate: 1.5,
  bloom: true,
  bloomThreshold: 1,
  bloomKnee: 0.5,
  bloomIntensity: 0.08,
  saturation: 1,
  contrast: 1,
  toneMapBackground: true,
})
export function copyPostProcessing(value) {
  const s = { ...defaultPostProcessing(), ...value }
  if (
    !toneMappings.includes(s.toneMapping) ||
    !['enabled', 'autoExposure', 'bloom', 'toneMapBackground'].every((k) => typeof s[k] === 'boolean')
  )
    throw Error('Invalid post-processing settings')
  for (const [k, min, max] of [
    ['exposureEV', -8, 8],
    ['minEV', -10, 10],
    ['maxEV', -10, 10],
    ['adaptationRate', 0.1, 10],
    ['bloomThreshold', 0, 20],
    ['bloomKnee', 0, 1],
    ['bloomIntensity', 0, 2],
    ['saturation', 0, 2],
    ['contrast', 0.1, 2],
  ])
    if (!Number.isFinite(s[k]) || s[k] < min || s[k] > max)
      throw Error('Invalid post-processing ' + k)
  if (s.minEV > s.maxEV) throw Error('Invalid exposure bounds')
  return s
}
export function postResourceSize(i, w, h) {
  if (i >= 23 && i <= 27) {
    const d = 2 ** (i - 23 + 1)
    return [Math.ceil(w / d), Math.ceil(h / d)]
  }
  if (i >= 28 && i <= 31) {
    const d = 2 ** (i - 28 + 1)
    return [Math.ceil(w / d), Math.ceil(h / d)]
  }
  if (i >= 32 && i <= 37) {
    const d = 4 ** (i - 32 + 1)
    return [Math.ceil(w / d), Math.ceil(h / d)]
  }
  if (i === 38 || i === 39) return [1, 1]
  return [w, h]
}
export function appendPostProcessing(passes, color, baseline, w, h, s, state, base, coverage = 16, coverageScale = 1) {
  if (!s.enabled) return { color, baseline }
  const u = [...base],
    now = performance.now(),
    dt = state.last ? Math.min(0.1, Math.max(1 / 240, (now - state.last) / 1000)) : 1 / 60
  state.last = now
  u.splice(
    44,
    4,
    1,
    toneMappings.indexOf(s.toneMapping),
    s.exposureEV,
    s.autoExposure ? 1 : 0,
  )
  u.splice(48, 4, s.bloom ? 1 : 0, s.bloomThreshold, s.bloomKnee, s.bloomIntensity)
  u.splice(52, 4, s.saturation, s.contrast, s.adaptationRate, dt)
  u.splice(56, 4, s.minEV, s.maxEV, base[58], 0)
  u.splice(60, 4, state.frames ? 1 : 0, 0, 0, 0)
  const add = (program, target, source) => {
    const p = { program, target, sources: Array(16).fill(16), u: [...u] }
    p.sources[0] = source
    passes.push(p)
    return p
  }
  let exposure = 16,
    bloom = 16
  if (s.autoExposure) {
    let rw = w,
      rh = h,
      input = color,
      level = 0
    do {
      const p = add(13, 32 + level, input)
      p.u[61] = level
      input = 32 + level++
      rw = Math.ceil(rw / 4)
      rh = Math.ceil(rh / 4)
    } while ((rw > 1 || rh > 1) && level < 6)
    exposure = state.frames % 2 ? 39 : 38
    add(14, exposure, input).sources[1] = state.frames % 2 ? 38 : 39
  }
  if (s.bloom && s.bloomIntensity > 0) {
    let input = color
    for (let i = 0; i < 5; i++) {
      add(11, 23 + i, input).u[61] = i === 0 ? 1 : 0
      input = 23 + i
    }
    for (let i = 3; i >= 0; i--) {
      add(12, 28 + i, 23 + i).sources[1] = input
      input = 28 + i
    }
    bloom = input
  }
  const compose = (source, target, useCoverage) => {
    const p = add(15, target, source)
    p.sources[1] = bloom
    p.sources[2] = exposure
    if (useCoverage && coverage !== 16) {
      p.sources[3] = coverage
      p.u[62] = coverageScale
    }
  }
  compose(color, 40, true)
  if (baseline !== 16) compose(baseline, 41, false)
  state.frames++
  return { color: 40, baseline: baseline === 16 ? 16 : 41 }
}
