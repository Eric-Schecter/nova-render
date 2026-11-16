export function copyFog(value = {}) {
  const fog = {
    mode: 'off',
    color: [0.3185468, 0.3185468, 0.3185468],
    nearDistance: 10,
    farDistance: 50,
    density: 0.02,
    ...value,
  }
  if (
    !['off', 'linear', 'exp2'].includes(fog.mode) ||
    !Array.isArray(fog.color) ||
    fog.color.length !== 3 ||
    !fog.color.every((v) => Number.isFinite(v) && v >= 0 && v <= 1) ||
    ![fog.nearDistance, fog.farDistance, fog.density].every(Number.isFinite) ||
    fog.nearDistance < 0 ||
    fog.farDistance <= fog.nearDistance ||
    fog.density < 0 ||
    fog.density > 1
  )
    throw Error('Invalid fog settings')
  fog.color = [...fog.color]
  return fog
}

export function copyShadows(value = {}) {
  const settings = { enabled: false, softness: 1, samples: 8, bias: 0.002, ...value }
  if (
    typeof settings.enabled !== 'boolean' ||
    !Number.isFinite(settings.softness) ||
    settings.softness < 0 ||
    settings.softness > 4 ||
    !Number.isInteger(settings.samples) ||
    settings.samples < 1 ||
    settings.samples > 16 ||
    !Number.isFinite(settings.bias) ||
    settings.bias < 0.00001 ||
    settings.bias > 0.1
  )
    throw Error('Invalid scene shadow settings')
  return settings
}
