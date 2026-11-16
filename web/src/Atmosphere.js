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
