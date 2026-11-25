import { emptyModelData } from './ModelData.js'
import { validateMaterial } from './MaterialData.js'
import { materialSlotLimit, modelVertexFloats } from './ModelVertex.js'

const vector = (value, length) =>
  (Array.isArray(value) || value instanceof Float32Array) &&
  value.length === length &&
  Array.from(value).every((x) => typeof x === 'number' && Number.isFinite(Math.fround(x)))

export function buildSceneGeometry({ vertices, materials = [], triangleMaterials = [] }) {
  if (
    !Array.isArray(vertices) ||
    !vertices.length ||
    vertices.length % 3 ||
    vertices.length > 10_000_000 ||
    !Array.isArray(materials) ||
    materials.length > materialSlotLimit ||
    !(Array.isArray(triangleMaterials) || triangleMaterials instanceof Uint32Array) ||
    (triangleMaterials.length && triangleMaterials.length !== vertices.length / 3)
  )
    throw Error('Invalid scene triangle geometry')

  const data = emptyModelData()
  data.materials = (materials.length ? materials : [{}]).map(validateMaterial)
  data.vertices = new Float32Array(vertices.length * modelVertexFloats)
  const min = [Infinity, Infinity, Infinity],
    max = [-Infinity, -Infinity, -Infinity]
  for (let i = 0; i < vertices.length; i++) {
    const { position, normal = [0, 1, 0], uv = [0, 0] } = vertices[i] ?? {}
    const slot = triangleMaterials.length ? triangleMaterials[Math.floor(i / 3)] : 0
    if (
      !vector(position, 3) ||
      !vector(normal, 3) ||
      !vector(uv, 2) ||
      !Number.isInteger(slot) ||
      slot < 0 ||
      slot >= data.materials.length
    )
      throw Error('Invalid scene vertex or material slot')
    const n = Array.from(normal, Math.fround),
      length = Math.hypot(...n)
    if (!length || !Number.isFinite(Math.fround(length))) throw Error('Invalid scene normal')
    const offset = i * modelVertexFloats
    for (let k = 0; k < 3; k++) {
      const p = Math.fround(position[k])
      data.vertices[offset + k] = p
      data.vertices[offset + 4 + k] = n[k] / length
      min[k] = Math.min(min[k], p)
      max[k] = Math.max(max[k], p)
    }
    data.vertices[offset + 3] = slot + 1
    data.vertices.set([uv[0], uv[1], 0, 1.5, uv[0], uv[1]], offset + 8)
    data.vertices.fill(1, offset + 16, offset + 20)
  }
  const extent = max.map((p, k) => p - min[k]),
    size = Math.max(...extent)
  if (!Number.isFinite(Math.fround(size)) || size <= 0) throw Error('Invalid scene geometry bounds')
  for (let i = 0; i < data.vertices.length; i += modelVertexFloats)
    for (let k = 0; k < 3; k++)
      data.vertices[i + k] = ((data.vertices[i + k] - min[k]) / size - (extent[k] / size) * 0.5) * 2
  data.sourceMinimum = min
  data.sourceMaximum = max
  return data
}
