// Matches render::DirectionalLight. Color is linear RGB, direction points toward the light.
export function defaultDirectionalLight() {
  return { direction: [-1, 1.6, 2], color: [1, 3.2 / 3.5, 0.8], intensity: 3.5 }
}
export function copyDirectionalLight(light) {
  if (
    !light ||
    !Array.isArray(light.direction) ||
    light.direction.length !== 3 ||
    !light.direction.every(Number.isFinite) ||
    !Number.isFinite(Math.hypot(...light.direction)) ||
    Math.hypot(...light.direction) < 1e-6
  )
    throw Error('Light direction must be a finite nonzero vec3 (length >= 1e-6)')
  if (
    !Array.isArray(light.color) ||
    light.color.length !== 3 ||
    !light.color.every((v) => Number.isFinite(v) && v >= 0 && v <= 1)
  )
    throw Error('Light color must be linear RGB in [0,1]')
  if (!Number.isFinite(light.intensity) || light.intensity < 0 || light.intensity > 100)
    throw Error('Light intensity must be in [0,100]')
  return { direction: [...light.direction], color: [...light.color], intensity: light.intensity }
}

// Scene lights use world space. Spot direction points outward; cone angles are radian half angles.
export function copySceneLights(input) {
  if (!Array.isArray(input) || input.length > 16) throw Error('Lights: maximum 16 entries')
  const ids = new Set()
  const vec = (v, name) => {
    if (!Array.isArray(v) || v.length !== 3 || !v.every(Number.isFinite))
      throw Error(`Invalid light ${name}`)
    return [...v]
  }
  return input.map((value) => {
    const light = {
      type: 'directional',
      enabled: true,
      color: [1, 1, 1],
      intensity: 1,
      position: [0, 0, 0],
      direction: [0, 1, 0],
      range: 0,
      innerConeAngle: 0.35,
      outerConeAngle: 0.6,
      castShadow: false,
      shadowSoftness: 1,
      ...value,
    }
    if (
      typeof light.id !== 'string' ||
      !light.id.length ||
      new TextEncoder().encode(light.id).length > 128 ||
      ids.has(light.id)
    )
      throw Error('Invalid or duplicate light id')
    ids.add(light.id)
    if (!['directional', 'point', 'spot'].includes(light.type)) throw Error('Invalid light type')
    if (typeof light.enabled !== 'boolean' || typeof light.castShadow !== 'boolean')
      throw Error('Invalid light flag')
    light.color = vec(light.color, 'color')
    if (!light.color.every((v) => v >= 0 && v <= 1))
      throw Error('Light color must be linear RGB in [0,1]')
    if (!Number.isFinite(light.intensity) || light.intensity < 0 || light.intensity > 100)
      throw Error('Light intensity must be in [0,100]')
    if (
      !Number.isFinite(light.shadowSoftness) ||
      light.shadowSoftness < 0 ||
      light.shadowSoftness > 4
    )
      throw Error('Invalid shadow softness')
    if (light.type !== 'directional') {
      light.position = vec(light.position, 'position')
      if (!Number.isFinite(light.range) || light.range < 0) throw Error('Invalid light range')
    }
    if (light.type !== 'point') {
      const d = vec(light.direction, 'direction'),
        length = Math.hypot(...d)
      if (!Number.isFinite(length) || length < 1e-6) throw Error('Invalid light direction')
      light.direction = d.map((v) => v / length)
    }
    if (
      light.type === 'spot' &&
      (!Number.isFinite(light.innerConeAngle) ||
        !Number.isFinite(light.outerConeAngle) ||
        light.innerConeAngle < 0 ||
        light.innerConeAngle > light.outerConeAngle ||
        light.outerConeAngle >= Math.PI / 2)
    )
      throw Error('Invalid cone angles')
    return {
      ...light,
      position: Array.isArray(light.position) ? [...light.position] : [0, 0, 0],
      direction: Array.isArray(light.direction) ? [...light.direction] : [0, 1, 0],
    }
  })
}
export function lightRecords(lights) {
  const enabled = (lights || []).filter((light) => light.enabled)
  const records = new Float32Array(enabled.length * 48)
  enabled.forEach((light, i) => {
    const start = i * 48
    records.set([...light.color, light.intensity], start + 32)
    records.set([...light.position, light.range], start + 36)
    records.set(
      [...light.direction, ['directional', 'point', 'spot'].indexOf(light.type)],
      start + 40,
    )
    records.set(
      [
        Math.cos(light.innerConeAngle),
        Math.cos(light.outerConeAngle),
        light.castShadow ? 1 : 0,
        light.shadowSoftness,
      ],
      start + 44,
    )
  })
  return records
}
