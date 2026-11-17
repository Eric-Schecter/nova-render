import { packMaterials, validateMaterial } from './MaterialData.js'
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
  shadowRecords = new Float32Array(0)
  pickInstances = []
  pick(ray, active = true) {
    return pickScene(this.meshes, this.pickInstances, ray, active)
  }
  sourceSpaces = new Map()
  assetBounds(id) {
    const m = this.meshes.get(id)
    if (!m) return null
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
          if (data.animation) throw Error('Scene instances currently require static GLB models')
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
        let offset = 0
        for (const m of next.values()) {
          m.first = offset / 12
          m.count = m.data.vertices.length / 12
          packed.vertices.set(m.data.vertices, offset)
          for (let i = offset + 3; i < offset + m.data.vertices.length; i += 12)
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
          const offset = packed.vertices[m.first * 12 + 3] - m.data.vertices[3]
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
    return { model: this.model, instances, draws, shadowRecords: this.shadowRecords }
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
    if (count * 48 > 128 * 1024 * 1024) throw Error('Ray scene geometry exceeds 128 MiB')
    const vertices = new Float32Array(Math.max(36, count * 12))
    let offset = 0
    for (const i of this.pickInstances) {
      const mesh = this.meshes.get(i.assetId),
        w = i.world,
        n = instanceNormal(w)
      if (n.every((v) => v === 0)) continue
      for (let j = 0; j < mesh.count; j++) {
        const at = (mesh.first + j) * 12,
          v = this.model.vertices.subarray(at, at + 12)
        vertices.set(v, offset)
        for (let k = 0; k < 3; k++)
          vertices[offset + k] = w[k] * v[0] + w[4 + k] * v[1] + w[8 + k] * v[2] + w[12 + k]
        const normal = [0, 1, 2].map((k) => n[k] * v[4] + n[4 + k] * v[5] + n[8 + k] * v[6]),
          length = Math.hypot(...normal)
        for (let k = 0; k < 3; k++) vertices[offset + 4 + k] = length ? normal[k] / length : 0
        vertices[offset + 10] = i.castShadow === false ? -1 : 0
        offset += 12
      }
    }
    if (!offset) {
      offset = 36
      for (let i = 0; i < 3; i++) {
        vertices[i * 12 + 3] = 1
        vertices[i * 12 + 5] = 1
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
