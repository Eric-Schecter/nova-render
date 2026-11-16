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
        !Array.isArray(i.world) ||
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
      let tile = 1
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
          for (const image of data.images) tile = Math.max(tile, image.width, image.height)
          next.set(id, { data, url: paths.get(id) })
        }
        tile = Math.min(tile, 2048)
        const cols = Math.max(1, Math.ceil(Math.sqrt(next.size))),
          rows = Math.max(1, Math.ceil(next.size / cols)),
          stride = tile + 4,
          width = cols * stride,
          height = rows * stride
        if (width > 8192 || height > 8192 || width * height * 20 > 512 * 1024 * 1024)
          throw Error('Scene material atlas exceeds 512 MiB / 8192px limit')
        const packed = emptyModelData()
        packed.images = Array.from({ length: 5 }, () => ({
          width,
          height,
          pixels: new Uint8Array(width * height * 4),
        }))
        let vertexCount = 0
        for (const m of next.values()) vertexCount += m.data.vertices.length
        packed.vertices = new Float32Array(vertexCount)
        let offset = 0,
          index = 0
        for (const m of next.values()) {
          const x = (index % cols) * stride,
            y = Math.floor(index / cols) * stride
          index++
          m.first = offset / 12
          m.count = m.data.vertices.length / 12
          packed.vertices.set(m.data.vertices, offset)
          offset += m.data.vertices.length
          m.uv = [(tile - 1) / width, (tile - 1) / height, (x + 2.5) / width, (y + 2.5) / height]
          for (let k = 0; k < 5; k++) {
            const src = m.data.images[k]
            let pixels = src.pixels
            if (src.bitmap) {
              const canvas = new OffscreenCanvas(src.width, src.height),
                ctx = canvas.getContext('2d')
              ctx.drawImage(src.bitmap, 0, 0)
              pixels = ctx.getImageData(0, 0, src.width, src.height).data
            }
            const dst = packed.images[k].pixels
            for (let py = 0; py < stride; py++) {
              const sy = Math.min(
                src.height - 1,
                Math.round(
                  Math.max(0, Math.min(1, (py - 2) / Math.max(1, tile - 1))) * (src.height - 1),
                ),
              )
              for (let px = 0; px < stride; px++) {
                const sx = Math.min(
                    src.width - 1,
                    Math.round(
                      Math.max(0, Math.min(1, (px - 2) / Math.max(1, tile - 1))) * (src.width - 1),
                    ),
                  ),
                  from = (sy * src.width + sx) * 4,
                  to = ((y + py) * width + x + px) * 4
                dst.set(pixels.subarray(from, from + 4), to)
              }
            }
          }
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
    const instances = new Float32Array(input.length * 48),
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
            ...m.uv,
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
    this.sourceSpaces = spaces
    return { model: this.model, instances, draws }
  }
  fork() {
    return Object.assign(new SceneData(), this)
  }
  dispose() {
    for (const m of this.meshes.values()) m.data.images.forEach((i) => i.bitmap?.close())
    this.meshes = new Map()
    this.sourceSpaces = new Map()
    this.key = ''
    this.model = null
  }
}
