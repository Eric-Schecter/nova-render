import { modelVertexFloats } from './ModelVertex.js'
import { multiply, nodeMatrix } from './ModelData.js'
const identity = () => [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
function normalize(q) {
  const n = Math.hypot(...q)
  if (n < 1e-8) throw Error('Degenerate animation vector')
  return q.map((v) => v / n)
}
function slerp(a, b, t) {
  a = normalize(a)
  b = normalize(b)
  let dot = a.reduce((s, v, i) => s + v * b[i], 0)
  if (dot < 0) {
    b = b.map((v) => -v)
    dot = -dot
  }
  if (dot > 0.9995) return normalize(a.map((v, i) => v + (b[i] - v) * t))
  const angle = Math.acos(Math.min(1, dot)),
    d = Math.sin(angle)
  return a.map((v, i) => (v * Math.sin((1 - t) * angle) + b[i] * Math.sin(t * angle)) / d)
}
export function readAnimation(json, bin) {
  if (!json.skins?.length && !json.animations?.length) return null
  const raw = new DataView(bin)
  function read(index, row, components, integer = false) {
    const a = json.accessors[index],
      v = json.bufferViews[a.bufferView],
      size = { 5121: 1, 5123: 2, 5126: 4 }[a.componentType],
      types = { 1: 'SCALAR', 3: 'VEC3', 4: 'VEC4', 16: 'MAT4' }
    if (a.sparse || !size || a.type !== types[components] || row >= a.count || v.buffer !== 0)
      throw Error('Invalid animation accessor')
    const stride = v.byteStride || size * components,
      start = (a.byteOffset || 0) + row * stride,
      offset = (v.byteOffset || 0) + start
    if (
      stride < size * components ||
      start + size * components > v.byteLength ||
      offset + size * components > bin.byteLength
    )
      throw Error('Animation accessor out of bounds')
    return Array.from({ length: components }, (_, k) => {
      let value
      if (a.componentType === 5126 && !integer) value = raw.getFloat32(offset + k * size, true)
      else if (a.componentType === 5121) value = raw.getUint8(offset + k)
      else if (a.componentType === 5123) value = raw.getUint16(offset + k * size, true)
      else throw Error('Unsupported animation component')
      if (a.normalized) {
        if (integer || a.componentType === 5126)
          throw Error('Invalid normalized animation accessor')
        value /= a.componentType === 5121 ? 255 : 65535
      }
      if (!Number.isFinite(value)) throw Error('Non-finite animation data')
      return value
    })
  }
  const nodes = json.nodes.map((n) => ({
    parent: -1,
    translation: [...(n.translation || [0, 0, 0])],
    rotation: normalize(n.rotation || [0, 0, 0, 1]),
    scale: [...(n.scale || [1, 1, 1])],
    matrix: n.matrix,
  }))
  json.nodes.forEach((n, i) => {
    for (const child of n.children || []) {
      if (!nodes[child] || nodes[child].parent !== -1) throw Error('Invalid animation hierarchy')
      nodes[child].parent = i
    }
  })
  for (const n of nodes) {
    let p = n.parent,
      depth = 0
    while (p >= 0) {
      if (++depth > nodes.length) throw Error('Animation hierarchy cycle')
      p = nodes[p].parent
    }
  }
  const skins = (json.skins || []).map((s) => {
    if (!s.joints.length || s.joints.some((j) => !nodes[j])) throw Error('Invalid skin joints')
    if (s.inverseBindMatrices !== undefined) {
      const a = json.accessors[s.inverseBindMatrices]
      if (a.componentType !== 5126 || a.count !== s.joints.length)
        throw Error('Invalid inverse bind matrices')
    }
    return {
      joints: s.joints,
      inverseBind: s.joints.map((_, i) =>
        s.inverseBindMatrices === undefined ? identity() : read(s.inverseBindMatrices, i, 16),
      ),
    }
  })
  for (let i = 0; i < nodes.length; i++) {
    const source = json.nodes[i], mesh = json.meshes?.[source.mesh]
    const count = mesh?.primitives[0]?.targets?.length ?? 0
    nodes[i].morphWeights = source.weights ?? mesh?.weights ?? Array(count).fill(0)
    if (nodes[i].morphWeights.length !== count || !nodes[i].morphWeights.every(Number.isFinite))
      throw Error('Invalid morph weights')
  }
  const tracks = [],
    clips = []
  for (const animation of json.animations || []) {
    let duration = 0
    const targets = new Set(),
      channels = animation.channels.map((c) => {
        const sampler = animation.samplers[c.sampler],
          node = c.target.node,
          path = c.target.path,
          key = node + ':' + path,
          interpolation = sampler.interpolation || 'LINEAR'
        if (!nodes[node] || (nodes[node].matrix && path !== "weights") || targets.has(key))
          throw Error('Invalid animation target')
        targets.add(key)
        if (
          !['translation', 'rotation', 'scale', 'weights'].includes(path) ||
          !['LINEAR', 'STEP', 'CUBICSPLINE'].includes(interpolation)
        )
          throw Error('Unsupported animation channel or interpolation mode')
        const input = json.accessors[sampler.input],
          output = json.accessors[sampler.output],
          cubic = interpolation === 'CUBICSPLINE',
          weightCount = path === 'weights' ? nodes[node].morphWeights.length : 1
        if (
          input.componentType !== 5126 ||
          output.componentType !== 5126 ||
          !input.count ||
          !weightCount || input.count * (cubic ? 3 : 1) * weightCount !== output.count
        )
          throw Error('Invalid animation sampler')
        const times = [],
          values = [],
          inTangents = [],
          outTangents = []
        for (let i = 0; i < input.count; i++) {
          const t = read(sampler.input, i, 1)[0]
          if (t < 0 || (i && t <= times[i - 1])) throw Error('Invalid animation times')
          times.push(t)
          const components = path === 'rotation' ? 4 : 3
          const key = cubic ? i * 3 + 1 : i
          const readKey = (row) => path === 'weights'
            ? Array.from({length: weightCount}, (_, k) => read(sampler.output, row * weightCount + k, 1)[0])
            : read(sampler.output, row, components)
          const value = readKey(key)
          if (cubic) {
            inTangents.push(readKey(key - 1))
            outTangents.push(readKey(key + 1))
          }
          if (path === 'rotation') normalize(value)
          values.push(value)
        }
        duration = Math.max(duration, times.at(-1))
        return { node, path, interpolation, times, values, inTangents, outTangents }
      })
    if (!channels.length || duration <= 0) throw Error('Empty animation clip')
    clips.push({ name: animation.name || 'Animation ' + (clips.length + 1), duration })
    tracks.push(channels)
  }
  return {
    nodes,
    skins,
    tracks,
    clips,
    vertices: [],
    center: [0, 0, 0],
    scale: 1,
    addVertex(skin, p, index, matrix, node, position, normal, deltas) {
      const v = {
        position: [...position],
        deltas,
        normal: [...normal],
        skin,
        node,
        matrix,
      }
      if (skin >= 0) {
        const s = skins[skin]
        if (!s) throw Error('Invalid skin')
        v.joints = read(p.attributes.JOINTS_0, index, 4, true)
        v.weights = read(p.attributes.WEIGHTS_0, index, 4)
        if (v.joints.some((j) => j >= s.joints.length) || v.weights.some((w) => w < 0))
          throw Error('Invalid skin influence')
        const total = v.weights.reduce((a, b) => a + b, 0)
        if (total < 1e-8) throw Error('Zero skin weights')
        v.weights = v.weights.map((w) => w / total)
      }
      this.vertices.push(v)
    },
  }
}
export function createAnimationPlayer(data) {
  let state = { clip: 0, time: 0, speed: 1, playing: true, loop: true }
  const a = data.animation
  const offsets = []
  let paletteCount = 0
  for (const skin of a?.skins || []) {
    offsets.push(paletteCount)
    paletteCount += skin.joints.length
  }
  const nodeOffset = paletteCount
  paletteCount += a?.nodes.length || 0
  if (a) {
    const morphOffsets = a.nodes.map((n) => {
      const offset = paletteCount
      paletteCount += Math.ceil(n.morphWeights.length / 16)
      return offset
    })
    let nextDelta = a.vertices.length
    const count = a.vertices.reduce((sum, vertex) => sum + vertex.deltas.length, nextDelta)
    if (count > 2 ** 24) throw Error("Morph vertex data exceeds the storage index range")
    const bytes = new ArrayBuffer(count * 64),
      floats = new Float32Array(bytes),
      words = new Uint32Array(bytes)
    a.vertices.forEach((v, i) => {
      floats.set([...v.position, 1, ...v.normal, v.deltas.length ? nextDelta + 1 : 0], i * 16)
      v.deltas.forEach(([position, normal], target) => {
        const start = nextDelta++ * 16
        floats.set(position, start)
        floats.set(normal, start + 4)
        words.set([morphOffsets[v.node] + Math.floor(target / 16), target % 16, v.deltas.length], start + 8)
      })
      for (let k = 0; k < 4; k++) {
        words[i * 16 + 8 + k] = v.skin >= 0 ? offsets[v.skin] + v.joints[k] : nodeOffset + v.node
        floats[i * 16 + 12 + k] = v.skin >= 0 ? v.weights[k] : k === 0 ? 1 : 0
      }
    })
    data.skinInputs = new Uint8Array(bytes)
    data.jointMatrices = new Float32Array(paletteCount * 16)
  }
  function apply() {
    if (a) data.jointMatrices.set(evaluateAnimation(data, state))
  }
  apply()
  return {
    get clips() {
      return a?.clips.map((c) => ({ ...c })) || []
    },
    get state() {
      return { ...state }
    },
    set(value) {
      const clip = a?.clips[value.clip]
      if (
        !clip ||
        !Number.isInteger(value.clip) ||
        !Number.isFinite(value.time) ||
        value.time < 0 ||
        value.time > clip.duration ||
        !Number.isFinite(value.speed) ||
        value.speed < 0.1 ||
        value.speed > 4 ||
        typeof value.playing !== 'boolean' ||
        typeof value.loop !== 'boolean'
      )
        throw Error('Invalid animation state')
      state = { ...value }
      apply()
    },
    update(delta) {
      if (!Number.isFinite(delta) || delta < 0) throw Error('Invalid animation delta')
      if (!a?.clips.length || !state.playing || !delta) return
      const duration = a.clips[state.clip].duration
      let time = state.time + delta * state.speed
      if (!Number.isFinite(time)) throw Error('Animation time overflow')
      if (state.loop) time %= duration
      else if (time >= duration) {
        time = duration
        state.playing = false
      }
      state.time = time
      apply()
    },
  }
}

export function evaluateAnimation(data, state) {
  const a = data.animation
  const count = a.nodes.length + a.skins.reduce((n, s) => n + s.joints.length, 0)
  const morphCount = a.nodes.reduce((n, node) => n + Math.ceil(node.morphWeights.length / 16), 0)
  const matrices = new Float32Array((count + morphCount) * 16)
  const nodes = a.nodes.map((n) => ({
    ...n,
    translation: [...n.translation],
    rotation: [...n.rotation],
    scale: [...n.scale],
  }))
  for (const track of a.tracks[state.clip] || []) {
    let r = track.times.findIndex((t) => t > state.time)
    if (r < 0) r = track.times.length - 1
    const l = r && state.time < track.times[r] ? r - 1 : r,
      t =
        l === r || track.interpolation === 'STEP'
          ? 0
          : (state.time - track.times[l]) / (track.times[r] - track.times[l])
    if (track.interpolation === 'CUBICSPLINE') {
      const dt = track.times[r] - track.times[l],
        t2 = t * t,
        t3 = t2 * t
      const value = track.values[l].map(
        (v, k) =>
          (2 * t3 - 3 * t2 + 1) * v +
          (t3 - 2 * t2 + t) * dt * track.outTangents[l][k] +
          (-2 * t3 + 3 * t2) * track.values[r][k] +
          (t3 - t2) * dt * track.inTangents[r][k],
      )
      nodes[track.node][track.path === 'weights' ? 'morphWeights' : track.path] = track.path === 'rotation' ? normalize(value) : value
      continue
    }
    nodes[track.node][track.path === 'weights' ? 'morphWeights' : track.path] =
      track.path === 'rotation'
        ? slerp(track.values[l], track.values[r], t)
        : track.values[l].map((v, k) => v + (track.values[r][k] - v) * t)
  }
  const world = []
  function global(i) {
    if (world[i]) return world[i]
    const n = nodes[i],
      local = nodeMatrix(n)
    return (world[i] = n.parent >= 0 ? multiply(global(n.parent), local) : local)
  }
  const normalization = identity()
  for (const i of [0, 5, 10]) normalization[i] = a.scale
  for (let k = 0; k < 3; k++) normalization[12 + k] = -a.center[k] * a.scale
  let index = 0
  const append = (matrix) => {
    const value = multiply(normalization, matrix)
    if (!value.every((v) => Number.isFinite(v) && Number.isFinite(Math.fround(v))))
      throw Error('Non-finite joint matrix')
    matrices.set(value, index * 16)
    index++
  }
  for (const skin of a.skins)
    skin.joints.forEach((joint, i) => append(multiply(global(joint), skin.inverseBind[i])))
  a.nodes.forEach((_, i) => append(global(i)))
  for (const node of nodes)
    for (let i = 0; i < node.morphWeights.length; i += 16)
      matrices.set(node.morphWeights.slice(i, i + 16), index++ * 16)
  return matrices
}

export function poseAnimationGeometry(source, palette) {
  const vertices = source.vertices.slice(),
    bytes = source.skinInputs
  const values = new Float32Array(bytes.buffer, bytes.byteOffset, bytes.byteLength / 4),
    joints = new Uint32Array(bytes.buffer, bytes.byteOffset, bytes.byteLength / 4)
  for (
    let vertex = 0, input = 0;
    vertex < vertices.length;
    vertex += modelVertexFloats, input += 16
  ) {
    let x = values[input], y = values[input + 1], z = values[input + 2]
    const first = values[input + 7] - 1
    if (first >= 0) {
      const count = joints[first * 16 + 10]
      for (let i = 0; i < count; i++) {
        const at = (first + i) * 16
        const weight = palette[joints[at + 8] * 16 + joints[at + 9]]
        x += values[at] * weight
        y += values[at + 1] * weight
        z += values[at + 2] * weight
      }
    }
    let px = 0,
      py = 0,
      pz = 0
    for (let k = 0; k < 4; k++) {
      const at = joints[input + 8 + k] * 16,
        weight = values[input + 12 + k]
      if (weight === 0) continue
      px +=
        weight * (palette[at] * x + palette[at + 4] * y + palette[at + 8] * z + palette[at + 12])
      py +=
        weight *
        (palette[at + 1] * x + palette[at + 5] * y + palette[at + 9] * z + palette[at + 13])
      pz +=
        weight *
        (palette[at + 2] * x + palette[at + 6] * y + palette[at + 10] * z + palette[at + 14])
    }
    vertices[vertex] = px
    vertices[vertex + 1] = py
    vertices[vertex + 2] = pz
  }
  return { ...source, vertices }
}
