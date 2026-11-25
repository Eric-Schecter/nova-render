const size = 512
const tile = 16
const grid = size / tile
const count = grid * grid
const materials = 16

const vertex = `
struct Vertex {
  @builtin(position) position: vec4f,
  @location(0) uv: vec2f,
  @location(1) @interpolate(flat) material: u32,
}
@vertex fn vs(@builtin(vertex_index) vertex: u32, @builtin(instance_index) instance: u32) -> Vertex {
  let quad = array<vec2f, 6>(vec2f(0,0), vec2f(1,0), vec2f(0,1),
    vec2f(0,1), vec2f(1,0), vec2f(1,1));
  let uv = quad[vertex];
  let cell = vec2f(f32(instance % ${grid}u), f32(instance / ${grid}u));
  let xy = (cell + uv) / ${grid}.0;
  return Vertex(vec4f(xy * vec2f(2,-2) + vec2f(-1,1), 0, 1), uv, instance % ${materials}u);
}
`

const visible = (id) => id % 4 !== 0
const cullShader = `
struct DrawArgs {
  vertexCount: u32,
  instanceCount: atomic<u32>,
  firstVertex: u32,
  firstInstance: u32,
}
@group(0) @binding(0) var<storage, read_write> visibleIds: array<u32>;
@group(0) @binding(1) var<storage, read_write> args: DrawArgs;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3u) {
  if (id.x >= ${count}u || id.x % 4u == 0u) { return; }
  let index = atomicAdd(&args.instanceCount, 1u);
  visibleIds[index] = id.x;
}
`

function shader(mode, compacted) {
  const xy = 'vec2i(input.uv * ' + tile + '.0)'
  const sample =
    mode === 'layers'
      ? `textureLoad(tex, ${xy}, i32(input.material), 0)`
      : mode === 'atlas'
        ? `textureLoad(tex, ${xy} + vec2i(i32(input.material) * ${tile}, 0), 0)`
        : `textureLoad(tex, ${xy}, 0)`
  return (
    (compacted
      ? '@group(0) @binding(1) var<storage, read> visibleIds: array<u32>;\n' +
        vertex.replace(
          'instance: u32) -> Vertex {',
          'drawIndex: u32) -> Vertex { let instance = visibleIds[drawIndex];',
        )
      : vertex) +
    `
@group(0) @binding(0) var tex: texture_${mode === 'layers' ? '2d_array' : '2d'}<f32>;
@fragment fn fs(input: Vertex) -> @location(0) vec4f { return ${sample}; }
`
  )
}

const median = (values) => [...values].sort((a, b) => a - b)[Math.floor(values.length / 2)]

export async function compareTextureBindings(canvas, { culling = false } = {}) {
  const adapter = await navigator.gpu?.requestAdapter()
  if (!adapter) throw new Error('WebGPU adapter unavailable')
  const timestamps = adapter.features.has('timestamp-query')
  const device = await adapter.requestDevice({
    requiredFeatures: timestamps ? ['timestamp-query'] : [],
  })
  try {
    const ctx = canvas.getContext('2d')
    if (!ctx) throw new Error('Canvas unavailable')
    canvas.width = canvas.height = size
    const usage = GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
    const texture = (width, layers = 1) =>
      device.createTexture({
        size: [width, tile, layers],
        format: 'rgba8unorm',
        usage,
      })
    const individual = Array.from({ length: materials }, () => texture(tile))
    const atlas = texture(tile * materials)
    const layers = texture(tile, materials)
    for (let id = 0; id < materials; id++) {
      const data = new Uint8Array(tile * tile * 4)
      for (let y = 0; y < tile; y++)
        for (let x = 0; x < tile; x++) {
          const p = (y * tile + x) * 4
          data.set([(id * 43 + x * 7) % 256, (id * 71 + y * 9) % 256, (x ^ y) * 16, 255], p)
        }
      for (const [target, origin] of [
        [individual[id], [0, 0, 0]],
        [atlas, [id * tile, 0, 0]],
        [layers, [0, 0, id]],
      ]) {
        device.queue.writeTexture({ texture: target, origin }, data, { bytesPerRow: tile * 4 }, [
          tile,
          tile,
        ])
      }
    }
    const target = device.createTexture({
      size: [size, size],
      format: 'rgba8unorm',
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
    })
    const targetView = target.createView()
    const read = device.createBuffer({
      size: size * size * 4,
      usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
    })
    const querySet = timestamps
      ? device.createQuerySet({ type: 'timestamp', count: culling ? 3 : 2 })
      : null
    const resolve = timestamps
      ? device.createBuffer({
          size: 24,
          usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC,
        })
      : null
    const timing = timestamps
      ? device.createBuffer({ size: 24, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ })
      : null
    const ids = culling ? new Uint32Array(count) : null
    const visibleIds = culling
      ? device.createBuffer({
          size: ids.byteLength,
          usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
        })
      : null
    const args = culling
      ? device.createBuffer({
          size: 16,
          usage: GPUBufferUsage.STORAGE | GPUBufferUsage.INDIRECT | GPUBufferUsage.COPY_DST,
        })
      : null
    let cullPipeline, cullGroup
    if (culling) {
      device.queue.writeBuffer(args, 0, new Uint32Array([6, 0, 0, 0]))
      cullPipeline = await device.createComputePipelineAsync({
        layout: 'auto',
        compute: { module: device.createShaderModule({ code: cullShader }) },
      })
      cullGroup = device.createBindGroup({
        layout: cullPipeline.getBindGroupLayout(0),
        entries: [
          { binding: 0, resource: { buffer: visibleIds } },
          { binding: 1, resource: { buffer: args } },
        ],
      })
    }

    const names = culling ? ['cpu-cull', 'gpu-cull'] : ['individual', 'atlas', 'layers']
    const modes = await Promise.all(
      names.map(async (name) => {
        const module = device.createShaderModule({
          code: shader(culling ? 'atlas' : name, culling),
        })
        const pipeline = await device.createRenderPipelineAsync({
          layout: 'auto',
          vertex: { module },
          fragment: { module, targets: [{ format: 'rgba8unorm' }] },
        })
        const views =
          name === 'individual'
            ? individual.map((t) => t.createView())
            : [
                name === 'layers'
                  ? layers.createView({ dimension: '2d-array' })
                  : atlas.createView(),
              ]
        const groups = views.map((resource) =>
          device.createBindGroup({
            layout: pipeline.getBindGroupLayout(0),
            entries: [
              { binding: 0, resource },
              ...(culling ? [{ binding: 1, resource: { buffer: visibleIds } }] : []),
            ],
          }),
        )
        return { name, pipeline, groups, cpu: [], gpu: [] }
      }),
    )

    async function run(mode, capture = false) {
      const start = performance.now()
      const encoder = device.createCommandEncoder()
      const gpuCull = mode.name === 'gpu-cull'
      let visibleCount = 0
      if (gpuCull) {
        encoder.clearBuffer(args, 4, 4)
        const compute = encoder.beginComputePass(
          timestamps && !capture
            ? { timestampWrites: { querySet, beginningOfPassWriteIndex: 0 } }
            : {},
        )
        compute.setPipeline(cullPipeline)
        compute.setBindGroup(0, cullGroup)
        compute.dispatchWorkgroups(Math.ceil(count / 64))
        compute.end()
      } else if (culling) {
        for (let i = 0; i < count; i++) if (visible(i)) ids[visibleCount++] = i
        device.queue.writeBuffer(visibleIds, 0, ids, 0, visibleCount)
      }
      const pass = encoder.beginRenderPass({
        colorAttachments: [
          { view: targetView, loadOp: 'clear', storeOp: 'store', clearValue: [0, 0, 0, 1] },
        ],
        ...(timestamps && !capture
          ? {
              timestampWrites: {
                querySet,
                beginningOfPassWriteIndex: gpuCull ? 1 : 0,
                endOfPassWriteIndex: gpuCull ? 2 : 1,
              },
            }
          : {}),
      })
      pass.setPipeline(mode.pipeline)
      if (mode.name !== 'individual') pass.setBindGroup(0, mode.groups[0])
      if (gpuCull) pass.drawIndirect(args, 0)
      else if (culling) pass.draw(6, visibleCount)
      else
        for (let i = 0; i < count; i++) {
          if (mode.name === 'individual') pass.setBindGroup(0, mode.groups[i % materials])
          pass.draw(6, 1, 0, i)
        }
      pass.end()
      if (capture)
        encoder.copyTextureToBuffer({ texture: target }, { buffer: read, bytesPerRow: size * 4 }, [
          size,
          size,
        ])
      else if (timestamps) {
        const queries = gpuCull ? 3 : 2
        encoder.resolveQuerySet(querySet, 0, queries, resolve, 0)
        encoder.copyBufferToBuffer(resolve, 0, timing, 0, queries * 8)
      }
      device.queue.submit([encoder.finish()])
      const cpu = performance.now() - start
      if (capture) {
        await read.mapAsync(GPUMapMode.READ)
        const pixels = new Uint8ClampedArray(read.getMappedRange().slice(0))
        read.unmap()
        return pixels
      }
      if (!timestamps) {
        await device.queue.onSubmittedWorkDone()
        return { cpu, gpu: null }
      }
      await timing.mapAsync(GPUMapMode.READ)
      const values = new BigUint64Array(timing.getMappedRange())
      const gpu = Number(values[gpuCull ? 2 : 1] - values[0]) / 1e6
      timing.unmap()
      return { cpu, gpu }
    }

    const reference = await run(modes[0], true)
    if (!reference.some((value, index) => index % 4 !== 3 && value !== 0))
      throw new Error('The reference image is blank')
    for (const mode of modes.slice(1)) {
      const pixels = await run(mode, true)
      if (pixels.some((value, index) => value !== reference[index]))
        throw new Error(`Image mismatch in ${mode.name}`)
    }
    ctx.putImageData(new ImageData(reference, size, size), 0, 0)

    for (let step = 0; step < 39; step++)
      for (let offset = 0; offset < modes.length; offset++) {
        const mode = modes[(step + offset) % modes.length]
        const { cpu, gpu } = await run(mode)
        if (step < 9) continue
        mode.cpu.push(cpu)
        if (gpu !== null) mode.gpu.push(gpu)
      }
    return modes.map(({ name, cpu, gpu, groups }) => ({
      name,
      draws: culling ? 1 : count,
      binds: groups.length === 1 ? 1 : count,
      cpuMs: median(cpu),
      gpuMs: gpu.length ? median(gpu) : null,
    }))
  } finally {
    device.destroy()
  }
}
