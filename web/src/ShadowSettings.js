export const defaultShadowSettings = () => ({
  method: 'pcss',
  resolution: 1024,
  depthBias: 0.001,
  normalBias: 0.015,
  filterRadius: 2,
  lightSize: 0.35,
  lightDirection: [-1.5, 3, 2],
})
export function copyShadowSettings(value) {
  const s = { ...defaultShadowSettings(), ...value }
  if (
    !['off', 'hard', 'pcf', 'pcss'].includes(s.method) ||
    ![512, 1024, 2048].includes(s.resolution) ||
    !Number.isFinite(s.depthBias) ||
    s.depthBias < 0 ||
    s.depthBias > 0.02 ||
    !Number.isFinite(s.normalBias) ||
    s.normalBias < 0 ||
    s.normalBias > 0.1 ||
    !Number.isFinite(s.filterRadius) ||
    s.filterRadius < 1 ||
    s.filterRadius > 6 ||
    !Number.isFinite(s.lightSize) ||
    s.lightSize < 0.01 ||
    s.lightSize > 1.5 ||
    !Array.isArray(s.lightDirection) ||
    s.lightDirection.length !== 3 ||
    !s.lightDirection.every(Number.isFinite) ||
    Math.hypot(...s.lightDirection) < 0.01
  )
    throw Error('Invalid shadow settings')
  return { ...s, lightDirection: [...s.lightDirection] }
}
