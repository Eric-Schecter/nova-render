import { modelVertexFloats, modelTriangleFloats } from './ModelVertex.js'
import { buildSceneSAH } from './SceneBVH.js'
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
const cross = (a, b) => [
  a[1] * b[2] - a[2] * b[1],
  a[2] * b[0] - a[0] * b[2],
  a[0] * b[1] - a[1] * b[0],
]
const sub = (a, b) => a.map((v, i) => v - b[i])
const length = (v) => Math.hypot(...v)
function box(low, high, o, d, limit) {
  let near = 0,
    far = limit
  for (let k = 0; k < 3; k++) {
    const pad = 1e-12 * Math.max(1, Math.abs(low[k]), Math.abs(high[k]))
    if (d[k] === 0) {
      if (o[k] < low[k] - pad || o[k] > high[k] + pad) return false
    } else {
      let a = (low[k] - pad - o[k]) / d[k],
        b = (high[k] + pad - o[k]) / d[k]
      if (a > b) [a, b] = [b, a]
      near = Math.max(near, a)
      far = Math.min(far, b)
      if (far < near) return false
    }
  }
  return true
}
export class PickMesh {
  constructor(data, strategy = 'median') {
    if (!data || data.vertices.length % modelTriangleFloats) throw Error('Invalid picking geometry')
    this.data = data
    this.triangles = []
    for (let i = 0; i < data.vertices.length / modelTriangleFloats; i++)
      if (data.vertices[i * modelTriangleFloats + 14] === 0) this.triangles.push(i)
    this.nodes = []
    if (strategy === 'sah') buildSceneSAH(data.vertices, this.triangles, this.nodes)
    else if (this.triangles.length) this.build(0, this.triangles.length)
  }
  point(triangle, vertex) {
    const offset = triangle * modelTriangleFloats + vertex * modelVertexFloats
    return Array.from(this.data.vertices.subarray(offset, offset + 3))
  }
  build(first, count) {
    const minimum = [Infinity, Infinity, Infinity],
      maximum = [-Infinity, -Infinity, -Infinity]
    for (let i = first; i < first + count; i++)
      for (let j = 0; j < 3; j++) {
        const p = this.point(this.triangles[i], j)
        for (let k = 0; k < 3; k++) {
          minimum[k] = Math.min(minimum[k], p[k])
          maximum[k] = Math.max(maximum[k], p[k])
        }
      }
    const index = this.nodes.length,
      node = { minimum, maximum, first, count }
    this.nodes.push(node)
    if (count > 8) {
      let axis = 0
      for (let k = 1; k < 3; k++)
        if (maximum[k] - minimum[k] > maximum[axis] - minimum[axis]) axis = k
      const centroid = (t) => {
        let v = 0
        for (let j = 0; j < 3; j++)
          v += this.data.vertices[t * modelTriangleFloats + j * modelVertexFloats + axis]
        return v
      }
      const sorted = this.triangles
        .slice(first, first + count)
        .sort((a, b) => centroid(a) - centroid(b) || a - b)
      for (let i = 0; i < count; i++) this.triangles[first + i] = sorted[i]
      const half = Math.floor(count / 2)
      node.left = this.build(first, half)
      node.right = this.build(first + half, count - half)
      node.count = 0
    }
    return index
  }
  refit(data) {
    if (data.vertices.length !== this.data.vertices.length)
      throw Error('Picking refit requires the same triangle topology')
    const result = Object.create(PickMesh.prototype)
    result.data = data
    result.triangles = this.triangles
    result.nodes = this.nodes.map((node) => ({
      ...node,
      minimum: [Infinity, Infinity, Infinity],
      maximum: [-Infinity, -Infinity, -Infinity],
    }))
    const vertices = data.vertices
    for (let n = result.nodes.length - 1; n >= 0; n--) {
      const { minimum, maximum, count, first, left, right } = result.nodes[n]
      if (!count) {
        for (let k = 0; k < 3; k++) {
          minimum[k] = Math.min(result.nodes[left].minimum[k], result.nodes[right].minimum[k])
          maximum[k] = Math.max(result.nodes[left].maximum[k], result.nodes[right].maximum[k])
        }
        continue
      }
      for (let i = first; i < first + count; i++) {
        const start = result.triangles[i] * modelTriangleFloats
        for (let v = start; v < start + modelTriangleFloats; v += modelVertexFloats)
          for (let k = 0; k < 3; k++) {
            minimum[k] = Math.min(minimum[k], vertices[v + k])
            maximum[k] = Math.max(maximum[k], vertices[v + k])
          }
      }
    }
    return result
  }
  writeShadowPose(records, offset = 0) {
    for (let i = 0; i < this.nodes.length; i++) {
      const at = offset + i * 48
      records.set(this.nodes[i].minimum, at + 32)
      records.set(this.nodes[i].maximum, at + 36)
    }
    const vertices = this.data.vertices
    for (let i = 0; i < this.triangles.length; i++) {
      const row = offset + (this.nodes.length + i) * 48
      for (let j = 0; j < 3; j++) {
        const v = (this.triangles[i] * 3 + j) * modelVertexFloats
        const position = row + 32 + j * 4
        records[position] = vertices[v]
        records[position + 1] = vertices[v + 1]
        records[position + 2] = vertices[v + 2]
      }
    }
  }
  shadowRecords() {
    const records = new Float32Array((this.nodes.length + this.triangles.length) * 48)
    if (records.byteLength > 64 * 1024 * 1024) throw Error('Scene shadow BVH exceeds 64 MiB')
    this.writeShadowPose(records)
    const escape = (index, next) => {
      const node = this.nodes[index],
        at = index * 48
      records[at + 35] = next
      records.set(
        [node.left || 0, node.right || 0, this.nodes.length + node.first, node.count],
        at + 44,
      )
      if (!node.count) {
        escape(node.left, node.right)
        escape(node.right, next)
      }
    }
    if (this.nodes.length) escape(0, this.nodes.length)
    this.triangles.forEach((t, i) => {
      const row = (this.nodes.length + i) * 48
      records[row + 6] = this.data.vertices[t * 3 * modelVertexFloats + 7]
      for (let j = 0; j < 3; j++) {
        const v = (t * 3 + j) * modelVertexFloats,
          vertices = this.data.vertices
        const colorOffset = row + (j < 2 ? 8 + j * 4 : 24)
        records.set(vertices.subarray(v + 16, v + 20), colorOffset)
        records[row + j * 2] = vertices[v + 12]
        records[row + j * 2 + 1] = vertices[v + 13]
        records[row + 43] = this.data.vertices[v + 3] - 1
        if (j < 2) {
          records[row + 44 + j * 2] = this.data.vertices[v + 8]
          records[row + 45 + j * 2] = this.data.vertices[v + 9]
        } else {
          records[row + 16] = this.data.vertices[v + 8]
          records[row + 21] = this.data.vertices[v + 9]
        }
      }
    })
    return records
  }
  pick(o, d, limit = Infinity) {
    if (!this.nodes.length) return null
    let best = null
    const stack = [0]
    while (stack.length) {
      const node = this.nodes[stack.pop()]
      if (!box(node.minimum, node.maximum, o, d, best?.distance ?? limit)) continue
      if (!node.count) {
        stack.push(node.right, node.left)
        continue
      }
      for (let i = node.first; i < node.first + node.count; i++) {
        const triangle = this.triangles[i],
          a = this.point(triangle, 0),
          e1 = sub(this.point(triangle, 1), a),
          e2 = sub(this.point(triangle, 2), a),
          normal = cross(e1, e2),
          p = cross(d, e2),
          det = dot(e1, p)
        if (Math.abs(det) <= 1e-12 * length(normal) * length(d)) continue
        const t = sub(o, a),
          u = dot(t, p) / det
        if (u < -1e-12 || u > 1 + 1e-12) continue
        const q = cross(t, e1),
          v = dot(d, q) / det
        if (v < -1e-12 || u + v > 1 + 1e-12) continue
        const distance = dot(e2, q) / det
        if (!Number.isFinite(distance) || distance < 0 || distance > limit) continue
        if (
          !best ||
          distance < best.distance ||
          (distance === best.distance && triangle < best.triangle)
        )
          best = { distance, normal, triangle }
      }
    }
    return best
  }
}
function vector(value) {
  if (
    !(Array.isArray(value) || value instanceof Float32Array) ||
    value.length !== 3 ||
    !Array.from(value).every(
      (v) => typeof v === 'number' && Number.isFinite(v) && Number.isFinite(Math.fround(v)),
    )
  )
    throw Error('Ray vectors require three finite float32 values')
  return Array.from(value, Math.fround)
}
export function pickScene(meshes, instances, ray, active = true) {
  if (
    !ray ||
    typeof ray !== 'object' ||
    Object.keys(ray).some((k) => !['origin', 'direction', 'includeIds', 'excludeIds'].includes(k))
  )
    throw Error('Invalid picking ray')
  const origin = vector(ray.origin),
    direction = vector(ray.direction),
    size = length(direction)
  if (size === 0) throw Error('Ray direction must be nonzero')
  const filter = (value) => {
    if (
      !Array.isArray(value) ||
      !Array.from(value).every((id) => typeof id === 'string' && id.length > 0)
    )
      throw Error('Ray filter IDs must be nonempty strings')
    return new Set(value)
  }
  const included = filter(ray.includeIds === undefined ? [] : ray.includeIds),
    excluded = filter(ray.excludeIds === undefined ? [] : ray.excludeIds)
  const unit = direction.map((v) => v / size)
  if (!active) return null
  let best = null
  for (const instance of instances) {
    if (excluded.has(instance.id) || (included.size && !included.has(instance.id))) continue
    const m = instance.world,
      a = m.slice(0, 3),
      b = m.slice(4, 7),
      c = m.slice(8, 11),
      co = [cross(b, c), cross(c, a), cross(a, b)],
      det = dot(a, co[0])
    if (det === 0) continue
    const inverse = (v) => co.map((row) => dot(row, v) / det)
    const hit = meshes
      .get(instance.assetId)
      .picking.pick(
        inverse(sub(origin, m.slice(12, 15))),
        inverse(unit),
        best?.distance ?? Infinity,
      )
    if (!hit || (best && hit.distance >= best.distance)) continue
    let normal = [0, 1, 2].map(
      (k) => co[0][k] * hit.normal[0] + co[1][k] * hit.normal[1] + co[2][k] * hit.normal[2],
    )
    const n = length(normal)
    if (n === 0 || !Number.isFinite(n)) continue
    normal = normal.map((v) => Math.fround(v / n))
    const position = origin.map((v, k) => Math.fround(v + unit[k] * hit.distance))
    if (!position.every(Number.isFinite)) continue
    best = { id: instance.id, distance: hit.distance, position, normal }
  }
  return best
}
