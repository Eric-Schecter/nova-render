const finiteFloat = (v) =>
  typeof v === 'number' && Number.isFinite(v) && Number.isFinite(Math.fround(v))
const vector = (v, n) =>
  (Array.isArray(v) || v instanceof Float32Array) &&
  v.length === n &&
  Array.from(v).every(finiteFloat)
export function copyViewCamera(value = {}) {
  if (
    !value ||
    typeof value !== 'object' ||
    Array.isArray(value) ||
    Object.keys(value).some(
      (k) =>
        ![
          'position',
          'orientation',
          'projection',
          'verticalFov',
          'orthographicHeight',
          'nearPlane',
          'farPlane',
          'exposure',
        ].includes(k),
    )
  )
    throw Error('Unknown camera setting')
  const c = {
    position: [0, 0, 3.6],
    orientation: [0, 0, 0, 1],
    projection: 'perspective',
    verticalFov: 45,
    orthographicHeight: 3,
    nearPlane: 0.01,
    farPlane: 200,
    exposure: 1,
    ...value,
  }
  if (!vector(c.position, 3) || !vector(c.orientation, 4))
    throw Error('Camera position/orientation must be finite vectors')
  const length = Math.hypot(...c.orientation)
  if (length < 1e-8) throw Error('Camera orientation must be a nonzero quaternion')
  if (
    !['perspective', 'orthographic'].includes(c.projection) ||
    ![c.verticalFov, c.orthographicHeight, c.nearPlane, c.farPlane, c.exposure].every(
      finiteFloat,
    ) ||
    c.verticalFov < 1 ||
    c.verticalFov > 179 ||
    c.orthographicHeight < 1e-5 ||
    c.nearPlane <= 0 ||
    c.farPlane <= c.nearPlane ||
    c.exposure <= 0 ||
    c.exposure > 16
  )
    throw Error('Invalid camera lens')
  return {
    ...c,
    position: Array.from(c.position),
    orientation: Array.from(c.orientation, (v) => v / length),
  }
}
export function cameraFromOrbit(value = {}) {
  const c = { yaw: 0, pitch: 0, distance: 3.6, exposure: 1, target: [0, 0, 0], ...value }
  if (
    ![c.yaw, c.pitch, c.distance, c.exposure].every(Number.isFinite) ||
    Math.abs(c.pitch) > 85 ||
    c.distance < 1.5 ||
    c.distance > 100 ||
    c.exposure <= 0 ||
    c.exposure > 16 ||
    !vector(c.target, 3)
  )
    throw Error('Invalid orbit camera')
  const yaw = (c.yaw * Math.PI) / 180,
    pitch = (c.pitch * Math.PI) / 180,
    d = c.distance
  const cy = Math.cos(yaw / 2),
    sy = Math.sin(yaw / 2),
    cx = Math.cos(pitch / 2),
    sx = Math.sin(pitch / 2)
  return copyViewCamera({
    position: [
      d * Math.cos(pitch) * Math.sin(yaw),
      d * Math.sin(pitch),
      d * Math.cos(pitch) * Math.cos(yaw),
    ].map((v, k) => v + (c.target?.[k] || 0)),
    orientation: [-sx * cy, cx * sy, sx * sy, cx * cy],
    exposure: c.exposure,
  })
}
export function cameraBasis(value) {
  const c = copyViewCamera(value),
    [x, y, z, w] = c.orientation
  const right = [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)]
  const up = [2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)]
  const forward = [-2 * (x * z + y * w), -2 * (y * z - x * w), -1 + 2 * (x * x + y * y)]
  return { c, right, up, forward }
}
export function copyViewport(value) {
  if (value === null) return null
  if (
    !value ||
    !['x', 'y', 'width', 'height'].every(
      (k) => Number.isInteger(value[k]) && value[k] >= 0 && value[k] <= 0xffffffff,
    ) ||
    !value.width ||
    !value.height
  )
    throw Error('Viewport requires nonnegative uint32 x/y and positive width/height')
  return { x: value.x, y: value.y, width: value.width, height: value.height }
}
export function resolveViewport(value, width, height) {
  const v = value || { x: 0, y: 0, width, height },
    x = Math.min(v.x, width),
    y = Math.min(v.y, height)
  return { x, y, width: Math.min(v.width, width - x), height: Math.min(v.height, height - y) }
}
