import { cameraFromOrbit, cameraBasis } from './Camera.js'
import { defaultEnvironment } from './EnvironmentData.js'
import { defaultDirectionalLight } from './Lighting.js'

export { loadGLB } from './GLTFLoader.js'

export const dot = (a, b) => a.reduce((s, x, i) => s + x * b[i], 0)

export function multiply(a, b) {
  const out = Array(16).fill(0)
  for (let c = 0; c < 4; c++)
    for (let r = 0; r < 4; r++)
      for (let k = 0; k < 4; k++) out[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k]
  return out
}
export function nodeMatrix(n) {
  if (n.matrix) return n.matrix
  const [x, y, z, w] = n.rotation || [0, 0, 0, 1],
    s = n.scale || [1, 1, 1],
    t = n.translation || [0, 0, 0]
  return [
    (1 - 2 * y * y - 2 * z * z) * s[0],
    (2 * x * y + 2 * z * w) * s[0],
    (2 * x * z - 2 * y * w) * s[0],
    0,
    (2 * x * y - 2 * z * w) * s[1],
    (1 - 2 * x * x - 2 * z * z) * s[1],
    (2 * y * z + 2 * x * w) * s[1],
    0,
    (2 * x * z + 2 * y * w) * s[2],
    (2 * y * z - 2 * x * w) * s[2],
    (1 - 2 * x * x - 2 * y * y) * s[2],
    0,
    ...t,
    1,
  ]
}
export function validateCamera(c) {
  if (
    c.target !== undefined &&
    (!Array.isArray(c.target) || c.target.length !== 3 || !c.target.every(Number.isFinite))
  )
    throw Error('Camera target must contain three finite values')
  if (
    ![c.yaw, c.pitch, c.distance, c.exposure].every(Number.isFinite) ||
    Math.abs(c.pitch) > 85 ||
    c.distance < 1.5 ||
    c.distance > 100 ||
    c.exposure <= 0 ||
    c.exposure > 16
  )
    throw Error('Invalid orbit camera (pitch +/-85, distance 1.5..100, exposure 0..16)')
}
export function sceneUniforms(
  data,
  camera,
  aspect,
  lighting = 'unlit',
  light = defaultDirectionalLight(),
  environment = null,
  settings = defaultEnvironment(),
) {
  const {
    c,
    right: s,
    up: u,
    forward: f,
  } = cameraBasis(camera.position ? camera : cameraFromOrbit(camera))
  const eye = c.position
  const view = [
    s[0],
    u[0],
    -f[0],
    0,
    s[1],
    u[1],
    -f[1],
    0,
    s[2],
    u[2],
    -f[2],
    0,
    -dot(s, eye),
    -dot(u, eye),
    dot(f, eye),
    1,
  ]
  const t = 1 / Math.tan((c.verticalFov * Math.PI) / 360),
    near = c.nearPlane,
    far = c.farPlane,
    half = c.orthographicHeight * 0.5
  const projection =
    c.projection === 'orthographic'
      ? [
          1 / (half * aspect),
          0,
          0,
          0,
          0,
          1 / half,
          0,
          0,
          0,
          0,
          1 / (near - far),
          0,
          0,
          0,
          near / (near - far),
          1,
        ]
      : [
          t / aspect,
          0,
          0,
          0,
          0,
          t,
          0,
          0,
          0,
          0,
          far / (near - far),
          -1,
          0,
          0,
          (near * far) / (near - far),
          0,
        ]
  const values = new Float32Array([
    ...multiply(projection, view),
    ...eye,
    c.exposure,
    ...data.baseColor,
    ...data.material,
    ...data.emissive.slice(0, 3),
    lighting === 'pbr' ? 1 : 0,
    ...light.direction.map((v) => v / Math.hypot(...light.direction)),
    0,
    ...light.color,
    light.intensity,
    ...s,
    (c.projection === 'orthographic' ? half : 1 / t) * aspect,
    ...u,
    c.projection === 'orthographic' ? -half : 1 / t,
    ...f,
    environment ? 1 : 0,
    Math.sin((settings.rotation * Math.PI) / 180),
    Math.cos((settings.rotation * Math.PI) / 180),
    settings.intensity,
    environment ? environment.mips.length - 1 : 0,
    settings.backgroundIntensity,
    settings.showBackground ? 1 : 0,
    0,
    data.skinInputs?.byteLength ? 1 : 0,
    ...(environment?.irradiance || new Float32Array(36)).map((v, i) => (i === 3 ? 0 : v)),
  ])
  values[67] = near
  values[71] = far
  return values
}

export function emptyModelData() {
  const colors = [
    [255, 255, 255, 255],
    [128, 128, 255, 255],
    [255, 255, 255, 255],
    [255, 255, 255, 255],
    [0, 0, 0, 255],
    [255, 255, 255, 255],
    [255, 255, 255, 255],
    [128, 128, 255, 255],
  ]
  return {
    vertices: new Float32Array(0),
    images: colors.map((color) => ({ width: 1, height: 1, pixels: new Uint8Array(color) })),
    baseColor: [1, 1, 1, 1],
    material: [1, 1, 1, 1],
    emissive: [0, 0, 0, 0],
  }
}
