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
        if (!nodes[node] || nodes[node].matrix || targets.has(key))
          throw Error('Invalid animation target')
        targets.add(key)
        if (
          !['translation', 'rotation', 'scale'].includes(path) ||
          !['LINEAR', 'STEP'].includes(interpolation)
        )
          throw Error('Animation supports TRS LINEAR/STEP only')
        const input = json.accessors[sampler.input],
          output = json.accessors[sampler.output]
        if (
          input.componentType !== 5126 ||
          output.componentType !== 5126 ||
          !input.count ||
          input.count !== output.count
        )
          throw Error('Invalid animation sampler')
        const times = [],
          values = []
        for (let i = 0; i < input.count; i++) {
          const t = read(sampler.input, i, 1)[0]
          if (t < 0 || (i && t <= times[i - 1])) throw Error('Invalid animation times')
          times.push(t)
          const value = read(sampler.output, i, path === 'rotation' ? 4 : 3)
          if (path === 'rotation') normalize(value)
          values.push(value)
        }
        duration = Math.max(duration, times.at(-1))
        return { node, path, interpolation, times, values }
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
    addVertex(skin, p, index, matrix, node, normal) {
      const v = {
        position: read(p.attributes.POSITION, index, 3),
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
    const bytes = new ArrayBuffer(a.vertices.length * 64),
      floats = new Float32Array(bytes),
      words = new Uint32Array(bytes)
    a.vertices.forEach((v, i) => {
      floats.set([...v.position, 1, ...v.normal, 0], i * 16)
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
  const matrices = new Float32Array(count * 16)
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
    nodes[track.node][track.path] =
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
  return matrices
}

export function poseAnimationGeometry(source, palette) {
  const vertices = source.vertices.slice(),
    bytes = source.skinInputs
  const values = new Float32Array(bytes.buffer, bytes.byteOffset, bytes.byteLength / 4),
    joints = new Uint32Array(bytes.buffer, bytes.byteOffset, bytes.byteLength / 4)
  for (let i = 0; i < vertices.length / modelVertexFloats; ++i)
    for (let axis = 0; axis < 3; ++axis) {
      let sum = 0
      for (let k = 0; k < 4; ++k) {
        const at = joints[i * 16 + 8 + k] * 16,
          weight = values[i * 16 + 12 + k]
        sum +=
          weight *
          (palette[at + axis] * values[i * 16] +
            palette[at + 4 + axis] * values[i * 16 + 1] +
            palette[at + 8 + axis] * values[i * 16 + 2] +
            palette[at + 12 + axis])
      }
      vertices[i * modelVertexFloats + axis] = sum
    }
  return { ...source, vertices }
}
