// Depth-first threaded BVH: escape links avoid a fixed GPU traversal stack.
// Leaves reorder whole triangles; material/normal/UV data stay together.
export function buildBVH(vertices) {
  if (!(vertices instanceof Float32Array) || !vertices.length || vertices.length % 36)
    throw Error('BVH requires non-empty triangle vertices')
  const triangles = Array.from({ length: vertices.length / 36 }, (_, id) => {
    const min = [Infinity, Infinity, Infinity],
      max = [-Infinity, -Infinity, -Infinity]
    for (let v = 0; v < 3; v++)
      for (let k = 0; k < 3; k++) {
        const p = vertices[id * 36 + v * 12 + k]
        if (!Number.isFinite(p)) throw Error('Non-finite BVH position')
        min[k] = Math.min(min[k], p)
        max[k] = Math.max(max[k], p)
      }
    return { id, min, max, center: min.map((p, k) => (p + max[k]) * 0.5) }
  })
  const nodes = [],
    ordered = []
  function split(items) {
    const node = {
      min: [Infinity, Infinity, Infinity],
      max: [-Infinity, -Infinity, -Infinity],
      first: 0,
      count: 0,
      escape: 0,
    }
    const index = nodes.length
    nodes.push(node)
    for (const t of items)
      for (let k = 0; k < 3; k++) {
        node.min[k] = Math.min(node.min[k], t.min[k])
        node.max[k] = Math.max(node.max[k], t.max[k])
      }
    if (items.length <= 4) {
      node.first = ordered.length
      node.count = items.length
      ordered.push(...items.map((t) => t.id))
    } else {
      const extent = node.max.map((x, k) => x - node.min[k])
      const axis = extent.indexOf(Math.max(...extent))
      items.sort((a, b) => a.center[axis] - b.center[axis] || a.id - b.id)
      const half = items.length >> 1
      split(items.slice(0, half))
      split(items.slice(half))
    }
    node.escape = nodes.length
    return index
  }
  split(triangles)
  const bytes = new ArrayBuffer(nodes.length * 48),
    floats = new Float32Array(bytes),
    words = new Uint32Array(bytes)
  nodes.forEach((n, i) => {
    floats.set(n.min, i * 12)
    words[i * 12 + 3] = n.escape
    floats.set(n.max, i * 12 + 4)
    words[i * 12 + 7] = n.first
    words[i * 12 + 8] = n.count
  })
  const reordered = new Float32Array(vertices.length)
  ordered.forEach((id, i) => reordered.set(vertices.subarray(id * 36, id * 36 + 36), i * 36))
  return {
    nodes: new Uint8Array(bytes),
    vertices: reordered,
    count: nodes.length,
    originalIds: ordered,
  }
}
