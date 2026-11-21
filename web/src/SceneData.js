import { modelVertexFloats, modelTriangleFloats } from './ModelVertex.js'
import { packMaterials, validateMaterial } from './MaterialData.js'
import { createAnimationPlayer, evaluateAnimation, poseAnimationGeometry } from './AnimationData.js'
import { PickMesh, pickScene } from './Picking.js'
import { loadGLB, emptyModelData, multiply } from './ModelData.js'
const cross = (a, b) => [
  a[1] * b[2] - a[2] * b[1],
  a[2] * b[0] - a[0] * b[2],
  a[0] * b[1] - a[1] * b[0],
]
export function instanceNormal(m) {
  const a = m.slice(0, 3),
    b = m.slice(4, 7),
    c = m.slice(8, 11),
    x = cross(b, c),
    y = cross(c, a),
    z = cross(a, b),
    det = a.reduce((s, v, i) => s + v * x[i], 0)
  if (Math.abs(det) < 1e-12) return Array(16).fill(0)
  return [
    ...x.map((v) => v / det),
    0,
    ...y.map((v) => v / det),
    0,
    ...z.map((v) => v / det),
    0,
    0,
    0,
    0,
    1,
  ]
}
export class SceneData {
  players = new Map()
  shadowPoseRevision = 0
  shadowRecords = new Float32Array(0)
  pickInstances = []
  pick(ray, active = true) {
    if (!active || !this.players.size)
      return pickScene(this.meshes, this.pickInstances, ray, active)
    const meshes = new Map(this.meshes),
      instances = this.pickInstances.map((instance) => {
        const player = this.players.get(instance.id)
        if (!player) return instance
        const geometry = this.poseGeometry(instance.id)
        player.queryPick ||= this.meshes.get(instance.assetId).picking.refit(geometry)
        const key = Symbol(instance.id)
        meshes.set(key, { picking: player.queryPick })
        return { ...instance, assetId: key }
      })
    return pickScene(meshes, instances, ray, active)
  }
  poseGeometry(id) {
    const p = this.players.get(id)
    if (p.query) return p.query
    return (p.query = poseAnimationGeometry(this.meshes.get(p.assetId).data, p.palette))
  }
  instanceBounds(id) {
    const instance = this.pickInstances.find((i) => i.id === id)
    if (!instance) return null
    const data = this.players.has(id)
      ? this.poseGeometry(id)
      : this.meshes.get(instance.assetId).data
    const minimum = [Infinity, Infinity, Infinity],
      maximum = [-Infinity, -Infinity, -Infinity],
      m = instance.world
    for (let i = 0; i < data.vertices.length; i += modelVertexFloats)
      for (let k = 0; k < 3; ++k) {
        const p =
          m[k] * data.vertices[i] +
          m[4 + k] * data.vertices[i + 1] +
          m[8 + k] * data.vertices[i + 2] +
          m[12 + k]
        minimum[k] = Math.min(minimum[k], p)
        maximum[k] = Math.max(maximum[k], p)
      }
    return { minimum, maximum }
  }
  sourceSpaces = new Map()
  assetBounds(id) {
    const m = this.meshes.get(id)
    if (!m) return null
    if (m.data.animation) {
      const minimum = [Infinity, Infinity, Infinity],
        maximum = [-Infinity, -Infinity, -Infinity]
      const half =
        Math.max(...m.data.sourceMaximum.map((v, k) => v - m.data.sourceMinimum[k])) * 0.5
      for (const [instance, p] of this.players)
        if (p.assetId === id) {
          const vertices = this.poseGeometry(instance).vertices
          for (let i = 0; i < vertices.length; i += modelVertexFloats)
            for (let k = 0; k < 3; ++k) {
              let value = vertices[i + k]
              if (this.sourceSpaces.get(id))
                value = value * half + (m.data.sourceMinimum[k] + m.data.sourceMaximum[k]) * 0.5
              minimum[k] = Math.min(minimum[k], value)
              maximum[k] = Math.max(maximum[k], value)
            }
        }
      return { minimum, maximum }
    }
    let minimum = [...m.data.sourceMinimum],
      maximum = [...m.data.sourceMaximum]
    if (!this.sourceSpaces.get(id)) {
      const half = Math.max(...maximum.map((v, k) => v - minimum[k])) * 0.5
      const center = minimum.map((v, k) => (v + maximum[k]) * 0.5)
      minimum = minimum.map((v, k) => (v - center[k]) / half)
      maximum = maximum.map((v, k) => (v - center[k]) / half)
    }
    return { minimum, maximum }
  }
  meshes = new Map()
  key = ''
  model = null
  async update(assets, input) {
    const next = this.fork()
    const result = await next.build(assets, input)
    Object.assign(this, next)
    return result
  }
  async build(assets, input) {
    if (!Array.isArray(assets) || !Array.isArray(input))
      throw Error('Scene assets and instances must be arrays')
    input = input.map(
      (i) =>
        i && {
          ...i,
          world: i.world === undefined ? [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1] : i.world,
        },
    )
    const paths = new Map()
    for (const a of assets) {
      if (
        !a ||
        typeof a.id !== 'string' ||
        !a.id ||
        paths.has(a.id) ||
        typeof a.url !== 'string' ||
        !a.url ||
        (a.sourceSpace !== undefined && typeof a.sourceSpace !== 'boolean')
      )
        throw Error('Scene assets require unique nonempty IDs, URLs and a boolean sourceSpace')
      paths.set(a.id, a.url)
    }
    const ids = new Set(),
      referenced = new Set()
    for (const i of input) {
      if (
        !i ||
        typeof i.id !== 'string' ||
        !i.id ||
        ids.has(i.id) ||
        !paths.has(i.assetId) ||
        (i.castShadow !== undefined && typeof i.castShadow !== 'boolean') ||
        !(Array.isArray(i.world) || i.world instanceof Float32Array) ||
        i.world.length !== 16 ||
        !Array.from(i.world).every((v) => Number.isFinite(v) && Number.isFinite(Math.fround(v))) ||
        [3, 7, 11].some((k) => Math.abs(i.world[k]) > 1e-6) ||
        Math.abs(i.world[15] - 1) > 1e-6
      )
        throw Error('Invalid scene instance')
      ids.add(i.id)
      referenced.add(i.assetId)
    }
    const key = JSON.stringify([...referenced].sort().map((id) => [id, paths.get(id)]))
    if (!this.model || key !== this.key) {
      const next = new Map()
      const fresh = []
      try {
        for (const id of [...referenced].sort()) {
          const previous = this.meshes.get(id)
          const data =
            previous?.url === paths.get(id) ? previous.data : await loadGLB(paths.get(id))
          if (data !== previous?.data) {
            fresh.push(data)
            for (const image of data.images)
              if (image.bitmap) {
                const canvas = new OffscreenCanvas(image.width, image.height)
                const context = canvas.getContext('2d')
                context.drawImage(image.bitmap, 0, 0)
                image.pixels = new Uint8Array(
                  context.getImageData(0, 0, image.width, image.height).data,
                )
                image.bitmap.close()
                delete image.bitmap
              }
          }
          if (data.animation && !data.skinInputs) createAnimationPlayer(data)
          next.set(id, {
            data,
            url: paths.get(id),
            picking: previous?.data === data ? previous.picking : new PickMesh(data),
          })
        }
        const packed = emptyModelData()
        packed.materials = []
        let vertexCount = 0
        for (const m of next.values()) vertexCount += m.data.vertices.length
        packed.vertices = new Float32Array(vertexCount)
        if ([...next.values()].some((m) => m.data.animation))
          packed.skinInputs = new Uint8Array((vertexCount / modelVertexFloats) * 64)
        let offset = 0
        for (const m of next.values()) {
          m.first = offset / modelVertexFloats
          m.count = m.data.vertices.length / modelVertexFloats
          packed.vertices.set(m.data.vertices, offset)
          if (m.data.skinInputs)
            packed.skinInputs.set(m.data.skinInputs, (offset / modelVertexFloats) * 64)
          for (let i = offset + 3; i < offset + m.data.vertices.length; i += modelVertexFloats)
            packed.vertices[i] += packed.materials.length
          packed.materials.push(...m.data.materials)
          offset += m.data.vertices.length
        }
        packMaterials(packed)
        let shadowCount = 0
        for (const m of next.values()) {
          m.shadowFirst = shadowCount / 48
          m.shadowData = m.picking.shadowRecords()
          m.shadowNodes = m.picking.nodes.length
          shadowCount += m.shadowData.length
        }
        if (shadowCount * 4 > 64 * 1024 * 1024) throw Error('Scene shadow BVHs exceed 64 MiB')
        this.shadowRecords = new Float32Array(shadowCount)
        for (const m of next.values()) {
          this.shadowRecords.set(m.shadowData, m.shadowFirst * 48)
          const offset = packed.vertices[m.first * modelVertexFloats + 3] - m.data.vertices[3]
          for (let row = m.shadowNodes; row < m.shadowData.length / 48; row++)
            this.shadowRecords[(m.shadowFirst + row) * 48 + 43] += offset
        }
        this.model = packed
        this.meshes = next
        this.key = key
      } catch (e) {
        fresh.forEach((data) => data.images.forEach((i) => i.bitmap?.close()))
        throw e
      }
    }
    const spaces = new Map(assets.map((a) => [a.id, !!a.sourceSpace]))
    const effective = input.map((i) => {
      if (!spaces.get(i.assetId)) return { ...i, world: [...i.world] }
      const d = this.meshes.get(i.assetId).data,
        min = d.sourceMinimum,
        max = d.sourceMaximum,
        half = Math.max(...max.map((v, k) => v - min[k])) * 0.5,
        center = min.map((v, k) => (v + max[k]) * 0.5)
      return {
        ...i,
        world: multiply(i.world, [half, 0, 0, 0, 0, half, 0, 0, 0, 0, half, 0, ...center, 1]),
      }
    })
    const players = new Map()
    let paletteCount = 0
    for (const i of input) {
      const data = this.meshes.get(i.assetId).data
      if (!data.animation) continue
      const old = this.players.get(i.id)
      const state =
        old && old.assetId === i.assetId && old.url === paths.get(i.assetId)
          ? { ...old.state }
          : { clip: 0, time: 0, speed: 1, playing: false, loop: true }
      const palette = evaluateAnimation(data, state)
      players.set(i.id, {
        assetId: i.assetId,
        url: paths.get(i.assetId),
        state,
        palette,
        paletteOffset: paletteCount / 16,
      })
      paletteCount += palette.length
      if (paletteCount * 4 > 64 * 1024 * 1024) throw Error('Scene animation palettes exceed 64 MiB')
    }
    const palette = new Float32Array(paletteCount)
    for (const p of players.values()) palette.set(p.palette, p.paletteOffset * 16)
    if (
      palette.length !== (this.model.jointMatrices?.length || 0) ||
      palette.some((v, i) => v !== this.model.jointMatrices[i])
    )
      this.model = { ...this.model, jointMatrices: palette }
    this.players = players
    const instances = new Float32Array(
        input.length * 48 + (this.model.materialRecords?.length || 0),
      ),
      draws = []
    let index = 0
    for (const [id, m] of this.meshes) {
      const first = index
      for (const i of effective)
        if (i.assetId === id) {
          const values = [
            ...i.world,
            ...instanceNormal(i.world),
            ...m.data.baseColor,
            ...m.data.material,
            ...m.data.emissive,
            input.length,
            m.shadowFirst,
            i.castShadow === false || instanceNormal(i.world).every((v) => v === 0) ? 0 : 1,
            m.shadowNodes,
          ]
          if (m.data.animation) {
            values[28] = players.get(i.id).paletteOffset + 1
          }
          if (!values.every((v) => Number.isFinite(v) && Number.isFinite(Math.fround(v))))
            throw Error('Instance transform overflows float32')
          instances.set(values, index * 48)
          index++
        }
      draws.push({
        firstVertex: m.first,
        vertexCount: m.count,
        firstInstance: first,
        instanceCount: index - first,
      })
    }
    instances.set(this.model.materialRecords || [], input.length * 48)
    this.assetsSnapshot = structuredClone(assets)
    this.inputSnapshot = structuredClone(input)
    this.pickInstances = effective.map((i) => ({ ...i, world: Array.from(i.world, Math.fround) }))
    this.sourceSpaces = spaces
    this.instanceBytes = instances
    this.draws = draws
    return { model: this.model, ...this.shadowInstances() }
  }
  shadowInstances() {
    if (!this.players.size)
      return { instances: this.instanceBytes, draws: this.draws, shadowRecords: this.shadowRecords }
    let cache = this.animationShadowCache
    if (!cache || cache.source !== this.instanceBytes) {
      const instances = this.instanceBytes.slice()
      const entries = []
      let length = this.shadowRecords.length,
        index = 0
      for (const [assetId, mesh] of this.meshes) {
        for (const instance of this.pickInstances) {
          if (instance.assetId !== assetId) continue
          const at = index++ * 48
          if (!this.players.has(instance.id) || !instances[at + 46]) continue
          instances[at + 45] = length / 48
          instances[at + 47] = mesh.shadowNodes
          entries.push({ id: instance.id, mesh, offset: length, pose: null })
          length += mesh.shadowData.length
        }
      }
      if (length * 4 > 64 * 1024 * 1024) throw Error('Scene shadow BVHs exceed 64 MiB')
      const records = new Float32Array(length)
      records.set(this.shadowRecords)
      for (const { mesh, offset } of entries) {
        const first = mesh.shadowFirst * 48
        records.set(this.shadowRecords.subarray(first, first + mesh.shadowData.length), offset)
      }
      this.animationShadowCache = cache = {
        source: this.instanceBytes,
        instances,
        entries,
        records,
      }
    }
    for (const entry of cache.entries) {
      const player = this.players.get(entry.id)
      player.queryPick ||= entry.mesh.picking.refit(this.poseGeometry(entry.id))
      if (entry.pose === player.queryPick) continue
      player.queryPick.writeShadowPose(cache.records, entry.offset)
      entry.pose = player.queryPick
    }
    return { instances: cache.instances, draws: this.draws, shadowRecords: cache.records }
  }
  animations(id) {
    const mesh = this.meshes.get(id)
    if (!mesh) throw Error('Unknown scene asset')
    return mesh.data.animation?.clips.map((c) => ({ ...c })) || []
  }
  animation(id) {
    const p = this.players.get(id)
    if (!p) throw Error('Instance has no animation')
    return { ...p.state }
  }
  setAnimation(id, value) {
    if (
      !value ||
      typeof value !== 'object' ||
      Array.isArray(value) ||
      Object.keys(value).some((k) => !['clip', 'time', 'speed', 'playing', 'loop'].includes(k))
    )
      throw Error('Invalid animation state')
    const p = this.players.get(id)
    if (!p) throw Error('Instance has no animation')
    const state = { ...p.state, ...value },
      clip = this.animations(p.assetId)[state.clip]
    if (
      !clip ||
      !Number.isInteger(state.clip) ||
      !Number.isFinite(state.time) ||
      state.time < 0 ||
      state.time > clip.duration ||
      !Number.isFinite(state.speed) ||
      state.speed < 0.1 ||
      state.speed > 4 ||
      typeof state.playing !== 'boolean' ||
      typeof state.loop !== 'boolean'
    )
      throw Error('Invalid animation state')
    const palette = evaluateAnimation(this.meshes.get(p.assetId).data, state)
    this.players = new Map(this.players)
    this.players.set(id, { ...p, state, palette, query: undefined, queryPick: undefined })
    this.shadowPoseRevision++
    this.commitPalettes()
  }
  updateAnimations(delta) {
    if (!Number.isFinite(delta) || !Number.isFinite(Math.fround(delta)) || delta < 0)
      throw Error('Invalid animation delta')
    const next = new Map()
    for (const [id, p] of this.players) {
      const state = { ...p.state }
      if (state.playing && delta) {
        let time = state.time + delta * state.speed
        const duration = this.animations(p.assetId)[state.clip].duration
        if (!Number.isFinite(Math.fround(time))) throw Error('Animation time overflow')
        if (state.loop) time %= duration
        else if (time >= duration) {
          time = duration
          state.playing = false
        }
        state.time = time
      }
      const palette = evaluateAnimation(this.meshes.get(p.assetId).data, state),
        changed = palette.some((v, i) => v !== p.palette[i])
      if (changed) this.shadowPoseRevision++
      next.set(id, {
        ...p,
        state,
        palette,
        query: changed ? undefined : p.query,
        queryPick: changed ? undefined : p.queryPick,
      })
    }
    this.players = next
    this.commitPalettes()
  }
  commitPalettes() {
    for (const p of this.players.values())
      this.model.jointMatrices.set(p.palette, p.paletteOffset * 16)
  }
  raySource = null
  rayCache = null
  rayKey = ''
  rayModel() {
    const key = JSON.stringify(
      this.pickInstances.map((i) => [i.assetId, i.world, i.castShadow !== false]),
    )
    if (this.rayCache && this.raySource === this.model && this.rayKey === key) return this.rayCache
    let count = 0
    for (const i of this.pickInstances) {
      const m = this.meshes.get(i.assetId)
      count += m.count
    }
    if (count * modelVertexFloats * Float32Array.BYTES_PER_ELEMENT > 128 * 1024 * 1024)
      throw Error('Ray scene geometry exceeds 128 MiB')
    const vertices = new Float32Array(Math.max(modelTriangleFloats, count * modelVertexFloats))
    let offset = 0
    for (const i of this.pickInstances) {
      const mesh = this.meshes.get(i.assetId),
        w = i.world,
        n = instanceNormal(w)
      if (n.every((v) => v === 0)) continue
      for (let j = 0; j < mesh.count; j++) {
        const at = (mesh.first + j) * modelVertexFloats,
          v = this.model.vertices.subarray(at, at + modelVertexFloats)
        vertices.set(v, offset)
        for (let k = 0; k < 3; k++)
          vertices[offset + k] = w[k] * v[0] + w[4 + k] * v[1] + w[8 + k] * v[2] + w[12 + k]
        const normal = [0, 1, 2].map((k) => n[k] * v[4] + n[4 + k] * v[5] + n[8 + k] * v[6]),
          length = Math.hypot(...normal)
        for (let k = 0; k < 3; k++) vertices[offset + 4 + k] = length ? normal[k] / length : 0
        vertices[offset + 10] = i.castShadow === false ? -1 : 0
        offset += modelVertexFloats
      }
    }
    if (!offset) {
      offset = modelTriangleFloats
      for (let i = 0; i < 3; i++) {
        vertices[i * modelVertexFloats + 3] = 1
        vertices[i * modelVertexFloats + 5] = 1
      }
    }
    const packed = vertices.slice(0, offset)
    if (!packed.every(Number.isFinite)) throw Error('Ray scene transform overflows float32')
    this.raySource = this.model
    this.rayKey = key
    this.rayCache = { ...this.model, vertices: packed }
    return this.rayCache
  }
  getMaterials(id) {
    const m = this.meshes.get(id)
    if (!m) throw Error('Unknown scene asset')
    return structuredClone(m.data.materials)
  }
  async setMaterial(id, slot, material) {
    const m = this.meshes.get(id)
    if (!m || !Number.isInteger(slot) || slot < 0 || slot >= m.data.materials.length)
      throw Error('Unknown scene material slot')
    const data = { ...m.data, materials: [...m.data.materials] }
    data.materials[slot] = validateMaterial(material)
    packMaterials(data)
    this.meshes = new Map(this.meshes)
    this.meshes.set(id, { ...m, data })
    this.model = null
    return this.build(this.assetsSnapshot, this.inputSnapshot)
  }
  fork() {
    return Object.assign(new SceneData(), this)
  }
  dispose() {
    this.players = new Map()
    this.animationShadowCache = undefined
    this.raySource = null
    this.rayCache = null
    this.rayKey = ''
    this.shadowRecords = new Float32Array(0)
    this.pickInstances = []
    for (const m of this.meshes.values()) m.data.images.forEach((i) => i.bitmap?.close())
    this.meshes = new Map()
    this.sourceSpaces = new Map()
    this.key = ''
    this.model = null
  }
}
