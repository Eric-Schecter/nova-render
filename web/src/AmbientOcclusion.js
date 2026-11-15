export const defaultAmbientOcclusion = () => ({
  enabled: false,
  method: 'ssao',
  output: 'composite',
  radius: 0.08,
  bias: 0.001,
  strength: 1,
  samples: 16,
  seed: 1,
  denoise: true,
  resolutionScale: 0.5,
})
export function copyAmbientOcclusion(value) {
  const defaults = defaultAmbientOcclusion(),
    s = { ...defaults, ...value }
  if (
    !value ||
    Object.keys(value).some((k) => !Object.hasOwn(defaults, k)) ||
    typeof s.enabled !== 'boolean' ||
    typeof s.denoise !== 'boolean' ||
    !['disabled', 'ssao', 'hbao', 'msao', 'rtao'].includes(s.method) ||
    !['composite', 'occlusion', 'depth', 'normals'].includes(s.output) ||
    !Number.isFinite(s.radius) ||
    s.radius < 0.001 ||
    s.radius > 1 ||
    !Number.isFinite(s.bias) ||
    s.bias < 0 ||
    s.bias > 0.05 ||
    !Number.isFinite(s.strength) ||
    s.strength < 0 ||
    s.strength > 4 ||
    !Number.isInteger(s.samples) ||
    s.samples < 4 ||
    s.samples > 64 ||
    !Number.isInteger(s.seed) ||
    s.seed < 0 ||
    s.seed > 4294967295 ||
    !Number.isFinite(s.resolutionScale) ||
    s.resolutionScale < 0.25 ||
    s.resolutionScale > 1
  )
    throw Error('Invalid ambient occlusion settings')
  return s
}
