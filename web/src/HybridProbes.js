import { modelVertexFloats } from './ModelVertex.js'
// The current buffer holds the entire grid. Only the scheduled range is written;
// a snapshot gives the update shader stable history for that range.
export const hybridProbeGridDimensions = Object.freeze([5, 4, 5])

export function createHybridProbes(device, pipeline, layout) {
  const dimensions = hybridProbeGridDimensions
  const count = dimensions.reduce((a, b) => a * b, 1)
  const size = count * 6 * 32
  const current = device.createBuffer({
    size,
    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST,
  })
  const previous = device.createBuffer({
    size,
    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
  })
  const config = device.createBuffer({
    size: 64,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
  })
  const bindings = (buffers) =>
    buffers.map((buffer, binding) => ({ binding: binding + 25, resource: { buffer } }))
  const batch = device.createBindGroup({
    layout: pipeline.getBindGroupLayout(2),
    entries: bindings([config, previous, current]),
  })
  const read = device.createBindGroup({ layout, entries: bindings([config, current]) })
  let first = 0
  let sequence = 0
  let reset = true
  return {
    read,
    invalidate() {
      reset = true
    },
    update(bounds, header, sceneResources, minimumExtent = 0.25, multiBounce = false) {
      if (!bounds) return
      const [minimum, maximum] = bounds
      const extent = maximum.map((v, i) => Math.max(v - minimum[i], minimumExtent))
      const spacing = extent.map((v, i) => v / dimensions[i])
      const origin = minimum.map((v, i) => v + spacing[i] * 0.5)
      const encoder = device.createCommandEncoder()
      if (reset) {
        encoder.clearBuffer(current)
        first = sequence = 0
        reset = false
      }
      const batchCount = Math.min(8, count - first)
      const configData = new ArrayBuffer(64)
      const floats = new Float32Array(configData)
      const ints = new Uint32Array(configData)
      floats.set(origin)
      floats.set([...spacing, Math.hypot(...extent)], 4)
      ints.set([...dimensions, first, batchCount, sequence], 8)
      floats[14] = 0.85
      ints[15] = header
      device.queue.writeBuffer(config, 0, configData)
      const bytesPerProbe = 6 * 32
      const offset = multiBounce ? 0 : first * bytesPerProbe
      encoder.copyBufferToBuffer(
        current,
        offset,
        previous,
        offset,
        multiBounce ? size : batchCount * bytesPerProbe,
      )
      const pass = encoder.beginComputePass()
      pass.setPipeline(pipeline)
      pass.setBindGroup(
        0,
        device.createBindGroup({
          layout: pipeline.getBindGroupLayout(0),
          entries: sceneResources,
        }),
      )
      pass.setBindGroup(2, batch)
      pass.dispatchWorkgroups(Math.ceil((batchCount * 6) / 32))
      pass.end()
      device.queue.submit([encoder.finish()])
      first = (first + batchCount) % count
      sequence++
    },
    dispose() {
      for (const buffer of [current, previous, config]) buffer.destroy()
    },
  }
}

export function probeSceneBounds(vertices, instances, draws) {
  const low = [Infinity, Infinity, Infinity]
  const high = [-Infinity, -Infinity, -Infinity]
  for (const draw of draws || [
    {
      firstVertex: 0,
      vertexCount: vertices.length / modelVertexFloats,
      firstInstance: 0,
      instanceCount: 1,
    },
  ]) {
    const minimum = [Infinity, Infinity, Infinity]
    const maximum = [-Infinity, -Infinity, -Infinity]
    if (!draw.vertexCount) continue
    for (let i = draw.firstVertex; i < draw.firstVertex + draw.vertexCount; i++) {
      for (let k = 0; k < 3; k++) {
        const value = vertices[i * modelVertexFloats + k]
        minimum[k] = Math.min(minimum[k], value)
        maximum[k] = Math.max(maximum[k], value)
      }
    }
    for (let i = draw.firstInstance; i < draw.firstInstance + draw.instanceCount; i++) {
      const base = i * 48
      for (let corner = 0; corner < 8; corner++) {
        const xyz = minimum.map((v, k) => (corner & (1 << k) ? maximum[k] : v))
        for (let k = 0; k < 3; k++) {
          const value = draws
            ? xyz.reduce(
                (sum, x, axis) => sum + x * instances[base + axis * 4 + k],
                instances[base + 12 + k],
              )
            : xyz[k]
          low[k] = Math.min(low[k], value)
          high[k] = Math.max(high[k], value)
        }
      }
    }
  }
  return low.every(Number.isFinite) ? [low, high] : null
}
