// Matches render::DirectionalLight. Color is linear RGB, direction points toward the light.
export function defaultDirectionalLight() {
  return { direction: [-1, 1.6, 2], color: [1, 3.2 / 3.5, 0.8], intensity: 3.5 }
}
export function copyDirectionalLight(light) {
  if (!light || !Array.isArray(light.direction) || light.direction.length !== 3 ||
      !light.direction.every(Number.isFinite) || !Number.isFinite(Math.hypot(...light.direction)) || Math.hypot(...light.direction) < 1e-6)
    throw Error('Light direction must be a finite nonzero vec3 (length >= 1e-6)')
  if (!Array.isArray(light.color) || light.color.length !== 3 || !light.color.every(v => Number.isFinite(v) && v >= 0 && v <= 1))
    throw Error('Light color must be linear RGB in [0,1]')
  if (!Number.isFinite(light.intensity) || light.intensity < 0 || light.intensity > 100)
    throw Error('Light intensity must be in [0,100]')
  return { direction: [...light.direction], color: [...light.color], intensity: light.intensity }
}
