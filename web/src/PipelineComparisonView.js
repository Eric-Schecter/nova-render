// Raster laboratory. The shared HLSL owns geometry, lighting and GPU light assignment.
export const rasterPipelines = ['forward', 'deferred', 'forward-plus', 'clustered-forward']
export const pipelineDebugViews = [
  'final',
  'base-color',
  'world-normal',
  'linear-depth',
  'roughness',
  'metallic',
  'light-count',
  'overflow',
]
export async function createPipelineComparisonView({
  canvas,
  shaderDirectory = '/generated/',
} = {}) {
  if (!canvas) throw Error('Pipeline comparison requires a canvas')
  const adapter = await navigator.gpu?.requestAdapter()
  if (!adapter) throw Error('WebGPU unavailable')
  const device = await adapter.requestDevice()
  const errors = []
  device.addEventListener('uncapturederror', (e) => errors.push(e.error))
  const context = canvas.getContext('webgpu')
  if (!context) {
    device.destroy()
    throw Error('WebGPU canvas unavailable')
  }
  const format = navigator.gpu.getPreferredCanvasFormat()
  context.configure({ device, format, alphaMode: 'opaque' })
  const uniform = device.createBuffer({
    size: 256 * 8,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
  })
  const layout = device.createBindGroupLayout({
    entries: [
      {
        binding: 0,
        visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
        buffer: { type: 'uniform', hasDynamicOffset: true, minBindingSize: 256 },
      },
      ...Array.from({ length: 7 }, (_, i) => ({
        binding: i + 1,
        visibility: GPUShaderStage.FRAGMENT,
        texture: { sampleType: 'unfilterable-float' },
      })),
    ],
  })
  const pipelineLayout = device.createPipelineLayout({ bindGroupLayouts: [layout] })
  const computeLayout = device.createBindGroupLayout({
    entries: [
      {
        binding: 0,
        visibility: GPUShaderStage.COMPUTE,
        buffer: { type: 'uniform', hasDynamicOffset: true, minBindingSize: 256 },
      },
      ...[5, 6].map((binding) => ({
        binding,
        visibility: GPUShaderStage.COMPUTE,
        texture: { sampleType: 'unfilterable-float' },
      })),
      {
        binding: 19,
        visibility: GPUShaderStage.COMPUTE,
        storageTexture: { access: 'write-only', format: 'rgba32float' },
      },
    ],
  })
  const computePipelineLayout = device.createPipelineLayout({ bindGroupLayouts: [computeLayout] })
  const pipelines = new Map()
  const texture = (w, h, depth = false, storage = false) =>
    device.createTexture({
      size: [w, h],
      format: depth ? 'depth32float' : storage ? 'rgba32float' : 'rgba16float',
      usage:
        GPUTextureUsage.RENDER_ATTACHMENT |
        (storage ? GPUTextureUsage.STORAGE_BINDING : 0) |
        (depth ? 0 : GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC),
    })
  let resources = [],
    depth,
    width = 0,
    height = 0,
    disposed = false
  let settings = {
    pipeline: 'forward',
    debug: 'final',
    lightCount: 128,
    lightRadius: 3,
    lightIntensity: 18,
  }
  let camera = { yaw: 25, pitch: 22, distance: 19, exposure: 1, target: [0, 0, 0] }
  try {
    for (let pass = 22; pass <= 28; pass++) {
      const response = await fetch(shaderDirectory + `aa-${pass}.json`)
      if (!response.ok) throw Error(`Missing pipeline shader ${pass}`)
      const shader = await response.json()
      if (pass === 25 || pass === 26) {
        pipelines.set(
          pass,
          await device.createComputePipelineAsync({
            layout: computePipelineLayout,
            compute: {
              module: device.createShaderModule({ code: shader.compute }),
              entryPoint: 'PipelineCSMain',
            },
          }),
        )
        continue
      }
      pipelines.set(
        pass,
        await device.createRenderPipelineAsync({
          layout: pipelineLayout,
          vertex: {
            module: device.createShaderModule({ code: shader.vert }),
            entryPoint: 'VSMain',
          },
          fragment: {
            module: device.createShaderModule({ code: shader.frag }),
            entryPoint: 'PSMain',
            targets: Array.from({ length: pass === 22 ? 4 : 1 }, () => ({
              format: pass === 28 ? format : 'rgba16float',
            })),
          },
          primitive: { topology: 'triangle-list', cullMode: 'none' },
          ...([22, 23, 27].includes(pass)
            ? {
                depthStencil: {
                  format: 'depth32float',
                  depthWriteEnabled: true,
                  depthCompare: 'less',
                },
              }
            : {}),
        }),
      )
    }
  } catch (e) {
    uniform.destroy()
    device.destroy()
    throw e
  }
  function setSettings(value) {
    const s = { ...settings, ...value }
    if (
      !rasterPipelines.includes(s.pipeline) ||
      !pipelineDebugViews.includes(s.debug) ||
      !Number.isInteger(s.lightCount) ||
      s.lightCount < 0 ||
      s.lightCount > 256 ||
      !Number.isFinite(s.lightRadius) ||
      s.lightRadius <= 0 ||
      s.lightRadius > 32 ||
      !Number.isFinite(s.lightIntensity) ||
      s.lightIntensity < 0 ||
      s.lightIntensity > 100
    )
      throw Error('Invalid pipeline comparison settings')
    settings = s
  }
  function setCamera(value) {
    const c = { ...camera, ...value, target: [...(value.target || camera.target)] }
    if (
      ![c.yaw, c.pitch, c.distance, ...c.target].every(Number.isFinite) ||
      c.target.length !== 3 ||
      Math.abs(c.pitch) > 80 ||
      c.distance < 4 ||
      c.distance > 50
    )
      throw Error('Invalid pipeline camera')
    camera = c
  }
  async function drawFrame(w = canvas.width, h = canvas.height) {
    if (disposed) throw Error('Pipeline view disposed')
    if (!Number.isInteger(w) || !Number.isInteger(h) || w < 16 || h < 9 || w > 2048 || h > 2048)
      throw Error('Pipeline dimensions must be 16x9 to 2048x2048')
    if (w !== width || h !== height) {
      resources.forEach((t) => t.destroy())
      depth?.destroy()
      width = w
      height = h
      canvas.width = w
      canvas.height = h
      resources = Array.from({ length: 9 }, (_, i) =>
        texture(
          i === 5 ? 16 : i === 6 ? 17 : i === 7 ? 1 : w,
          i === 5 ? 9 : i === 6 ? 2304 : i === 7 ? 1 : h,
          false,
          i === 5 || i === 6,
        ),
      )
      depth = texture(w, h, true)
    }
    const u = new Float32Array(64)
    u.set([
      w,
      h,
      rasterPipelines.indexOf(settings.pipeline),
      pipelineDebugViews.indexOf(settings.debug),
    ])
    u.set([settings.lightCount, settings.lightRadius, 0, 0], 4)
    u.set([(camera.yaw * Math.PI) / 180, (camera.pitch * Math.PI) / 180, camera.distance, 0], 8)
    u.set(camera.target, 12)
    u[16] = settings.lightIntensity
    u[42] = 51 * 36
    const commands = device.createCommandEncoder()
    // Initialize dummy bindings explicitly, including the first frame.
    commands
      .beginRenderPass({
        colorAttachments: [
          {
            view: resources[7].createView(),
            clearValue: [0, 0, 0, 0],
            loadOp: 'clear',
            storeOp: 'store',
          },
        ],
      })
      .end()
    let index = 0
    function pass(program, target) {
      const offset = index++ * 256
      device.queue.writeBuffer(uniform, offset, u)
      if (program === 25 || program === 26) {
        const render = commands.beginComputePass()
        const group = device.createBindGroup({
          layout: computeLayout,
          entries: [
            { binding: 0, resource: { buffer: uniform, size: 256 } },
            { binding: 5, resource: resources[4].createView() },
            { binding: 6, resource: resources[program === 26 ? 7 : 5].createView() },
            { binding: 19, resource: resources[target].createView() },
          ],
        })
        render.setPipeline(pipelines.get(program))
        render.setBindGroup(0, group, [offset])
        render.dispatchWorkgroups(
          16,
          9,
          program === 25 && settings.pipeline === 'clustered-forward' ? 16 : 1,
        )
        render.end()
        return
      }
      const count = program === 22 ? 4 : 1
      const isScene = [22, 23, 27].includes(program)
      const attachments = Array.from({ length: count }, (_, a) => ({
        view:
          target < 0
            ? context.getCurrentTexture().createView()
            : resources[target + a].createView(),
        clearValue: target < 0 || program === 23 ? [0.015, 0.02, 0.03, 1] : [0, 0, 0, 0],
        loadOp: 'clear',
        storeOp: 'store',
      }))
      const render = commands.beginRenderPass({
        colorAttachments: attachments,
        ...(isScene
          ? {
              depthStencilAttachment: {
                view: depth.createView(),
                depthClearValue: 1,
                depthLoadOp: 'clear',
                depthStoreOp: 'discard',
              },
            }
          : {}),
      })
      const group = device.createBindGroup({
        layout,
        entries: [
          { binding: 0, resource: { buffer: uniform, size: 256 } },
          ...Array.from({ length: 7 }, (_, i) => ({
            binding: i + 1,
            resource:
              resources[
                program === 28 && i === 0
                  ? 8
                  : target >= 0 && i >= target && i < target + count
                    ? 7
                    : i
              ].createView(),
          })),
        ],
      })
      render.setPipeline(pipelines.get(program))
      render.setBindGroup(0, group, [offset])
      render.draw(isScene ? 51 * 36 : 3)
      render.end()
    }
    if (settings.pipeline === 'forward-plus') {
      pass(27, 4)
      pass(26, 5)
      pass(25, 6)
    }
    if (settings.pipeline === 'clustered-forward') pass(25, 6)
    if (settings.pipeline === 'deferred') {
      pass(22, 0)
      pass(24, 8)
    } else pass(23, 8)
    pass(28, -1)
    device.queue.submit([commands.finish()])
    await device.queue.onSubmittedWorkDone()
    if (errors.length) throw errors.shift()
  }
  async function readPixels() {
    if (!width || disposed) throw Error('Draw a frame before reading pixels')
    const rowBytes = Math.ceil((width * 8) / 256) * 256
    const buffer = device.createBuffer({
      size: rowBytes * height,
      usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
    })
    const encoder = device.createCommandEncoder()
    encoder.copyTextureToBuffer({ texture: resources[8] }, { buffer, bytesPerRow: rowBytes }, [
      width,
      height,
    ])
    device.queue.submit([encoder.finish()])
    try {
      await buffer.mapAsync(GPUMapMode.READ)
      return { width, height, rowBytes, pixels: new Uint8Array(buffer.getMappedRange()).slice() }
    } finally {
      buffer.destroy()
    }
  }
  return {
    readPixels,
    setSettings,
    getSettings: () => ({ ...settings }),
    setCamera,
    getCamera: () => ({ ...camera, target: [...camera.target] }),
    drawFrame,
    dispose() {
      if (disposed) return
      disposed = true
      resources.forEach((t) => t.destroy())
      depth?.destroy()
      uniform.destroy()
      context.unconfigure()
      device.destroy()
    },
  }
}
