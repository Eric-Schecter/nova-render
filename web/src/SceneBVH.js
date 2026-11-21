import { modelVertexFloats, modelTriangleFloats } from './ModelVertex.js'

const binCount = 12
const leafSize = 8

function area(bounds, offset) {
  const x = bounds[offset + 3] - bounds[offset]
  const y = bounds[offset + 4] - bounds[offset + 1]
  const z = bounds[offset + 5] - bounds[offset + 2]
  return x * y + y * z + z * x
}

function clearBounds(bounds, offset) {
  bounds.fill(Infinity, offset, offset + 3)
  bounds.fill(-Infinity, offset + 3, offset + 6)
}

function extendBounds(target, at, source, from) {
  for (let axis = 0; axis < 3; axis++) {
    target[at + axis] = Math.min(target[at + axis], source[from + axis])
    target[at + axis + 3] = Math.max(target[at + axis + 3], source[from + axis + 3])
  }
}

// Binned surface-area heuristic. Geometry order and the GPU record format are unchanged
// apart from the permutation of primitives and the resulting bounds hierarchy.
export function buildSceneSAH(vertices, triangles, nodes) {
  if (!triangles.length) return
  const triangleCapacity = vertices.length / modelTriangleFloats
  const bounds = new Float32Array(triangleCapacity * 6)
  const centers = new Float32Array(triangleCapacity * 3)
  for (const i of triangles) {
    const first = i * modelTriangleFloats
    for (let axis = 0; axis < 3; axis++) {
      const a = vertices[first + axis]
      const b = vertices[first + modelVertexFloats + axis]
      const c = vertices[first + 2 * modelVertexFloats + axis]
      bounds[i * 6 + axis] = Math.min(a, b, c)
      bounds[i * 6 + axis + 3] = Math.max(a, b, c)
      centers[i * 3 + axis] = (a + b + c) / 3
    }
  }
  const bins = new Float64Array(binCount * 6)
  const counts = new Uint32Array(binCount)
  const leftCosts = new Float64Array(binCount - 1)
  const merged = new Float64Array(6)
  const rangeBounds = new Float64Array(6)
  const scratch = new Uint32Array(triangles.length)
  const bucket = (triangle, axis, low, scale) =>
    Math.min(binCount - 1, Math.floor((centers[triangle * 3 + axis] - low) * scale))

  function build(first, count, depth) {
    const end = first + count
    clearBounds(rangeBounds, 0)
    const low = [Infinity, Infinity, Infinity]
    const high = [-Infinity, -Infinity, -Infinity]
    for (let i = first; i < end; i++) {
      const triangle = triangles[i]
      extendBounds(rangeBounds, 0, bounds, triangle * 6)
      for (let axis = 0; axis < 3; axis++) {
        const center = centers[triangle * 3 + axis]
        low[axis] = Math.min(low[axis], center)
        high[axis] = Math.max(high[axis], center)
      }
    }
    const index = nodes.length
    const node = {
      minimum: Array.from(rangeBounds.subarray(0, 3)),
      maximum: Array.from(rangeBounds.subarray(3)),
      first,
      count,
    }
    nodes.push(node)
    if (count <= leafSize) return index

    let best = Infinity
    let splitAxis = -1
    let splitBucket = 0
    let splitScale = 0
    // Bound recursion even when geometry repeatedly produces a very asymmetric split.
    for (let axis = 0; axis < 3 && depth < 32; axis++) {
      const span = high[axis] - low[axis]
      if (!(span > 0)) continue
      const scale = binCount / span
      counts.fill(0)
      for (let bin = 0; bin < binCount; bin++) clearBounds(bins, bin * 6)
      for (let i = first; i < end; i++) {
        const triangle = triangles[i]
        const bin = bucket(triangle, axis, low[axis], scale)
        counts[bin]++
        extendBounds(bins, bin * 6, bounds, triangle * 6)
      }
      clearBounds(merged, 0)
      let left = 0
      for (let bin = 0; bin < binCount - 1; bin++) {
        if (counts[bin]) extendBounds(merged, 0, bins, bin * 6)
        left += counts[bin]
        leftCosts[bin] = left ? area(merged, 0) * left : Infinity
      }
      clearBounds(merged, 0)
      let right = 0
      for (let bin = binCount - 1; bin > 0; bin--) {
        if (counts[bin]) extendBounds(merged, 0, bins, bin * 6)
        right += counts[bin]
        if (!right) continue
        const cost = leftCosts[bin - 1] + area(merged, 0) * right
        if (cost < best) {
          best = cost
          splitAxis = axis
          splitBucket = bin - 1
          splitScale = scale
        }
      }
    }
    let half = Math.floor(count / 2)
    if (splitAxis >= 0) {
      let left = first
      let right = end
      for (let i = first; i < end; i++) {
        const triangle = triangles[i]
        if (bucket(triangle, splitAxis, low[splitAxis], splitScale) <= splitBucket)
          scratch[left++] = triangle
        else scratch[--right] = triangle
      }
      half = left - first
      for (let i = first; i < left; i++) triangles[i] = scratch[i]
      for (let i = left; i < end; i++) triangles[i] = scratch[end - 1 - (i - left)]
    }
    node.left = build(first, half, depth + 1)
    node.right = build(first + half, count - half, depth + 1)
    node.count = 0
    return index
  }
  build(0, triangles.length, 0)
}
