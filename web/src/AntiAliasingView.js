// Raster-only AA laboratory. All scene shading and reconstruction comes from shared HLSL.
const names = ['off', 'fxaa', 'smaa', 'msaa', 'ssaa', 'taa']
const C = 0,
  M = 1,
  E = 2,
  W = 3,
  HA = 4,
  HB = 5,
  PM = 6,
  F = 7,
  O = 8,
  B = 9,
  BM = 10,
  G = 11,
  AREA = 12,
  SEARCH = 13,
  MC = 14,
  MM = 15,
  DUMMY = 16
const halton = (index, base) => {
  let f = 1,
    v = 0
  while (index) {
    f /= base
    v += f * (index % base)
    index = Math.floor(index / base)
  }
  return v
}
export async function createAntiAliasingView({ canvas, shaderDirectory = '/generated/' } = {}) {
  if (!canvas) throw Error('AA requires a canvas')
  const adapter = await navigator.gpu?.requestAdapter()
  if (!adapter) throw Error('WebGPU unavailable')
  const device = await adapter.requestDevice(),
    context = canvas.getContext('webgpu')
  if (!context) {
    device.destroy()
    throw Error('WebGPU canvas unavailable')
  }
  const format = navigator.gpu.getPreferredCanvasFormat()
  let disposed = false,
    frames = 0,
    width = 0,
    height = 0,
    superSample = false,
    textures = [],
    depth,
    depthMS,
    depthBaseline
  let settings = {
      method: 'taa',
      historyWeight: 0.9,
      zoom: 1,
      split: true,
      textureFiltering: true,
    },
    camera = { yaw: 20, pitch: 15, distance: 6.2, exposure: 1 },
    previous = { ...camera },
    time = 0,
    previousTime = 0,
    previousJitter = [0, 0]
  let tail = Promise.resolve(),
    lost
  device.lost.then((info) => {
    lost = info.message
  })
  const check = () => {
    if (disposed || lost) throw Error(lost || 'AA view disposed')
  }
  const texture = (w, h, format = 'rgba16float', sampleCount = 1) =>
    device.createTexture({
      size: [w, h],
      format,
      sampleCount,
      usage:
        GPUTextureUsage.RENDER_ATTACHMENT |
        GPUTextureUsage.TEXTURE_BINDING |
        (sampleCount === 1 ? GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST : 0),
    })
  let uniform,
    layout,
    pipelines = [],
    sceneMS,
    displayPipeline,
    linear,
    point
  try {
    uniform = device.createBuffer({
      size: 256 * 16,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    })
    layout = device.createBindGroupLayout({
      entries: Array.from({ length: 11 }, (_, i) => ({
        binding: i,
        visibility:
          i === 0 ? GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT : GPUShaderStage.FRAGMENT,
        ...(i === 0
          ? { buffer: { type: 'uniform', hasDynamicOffset: true, minBindingSize: 128 } }
          : i < 9
            ? { texture: { sampleType: 'float' } }
            : { sampler: { type: i === 9 ? 'filtering' : 'non-filtering' } }),
      })),
    })
    const pipelineLayout = device.createPipelineLayout({ bindGroupLayouts: [layout] })
    async function pipeline(program, count = 1, targetFormat = 'rgba16float') {
      const response = await fetch(shaderDirectory + `aa-${program}.json`)
      if (!response.ok) throw Error('Missing AA HLSL shader ' + program)
      const data = await response.json()
      return device.createRenderPipelineAsync({
        layout: pipelineLayout,
        vertex: { module: device.createShaderModule({ code: data.vert }), entryPoint: 'VSMain' },
        fragment: {
          module: device.createShaderModule({ code: data.frag }),
          entryPoint: 'PSMain',
          targets: Array.from({ length: program === 0 ? 2 : 1 }, () => ({ format: targetFormat })),
        },
        primitive: { topology: 'triangle-list', cullMode: 'none' },
        multisample: { count },
        ...(program === 0
          ? {
              depthStencil: {
                format: 'depth32float',
                depthWriteEnabled: true,
                depthCompare: 'less',
              },
            }
          : {}),
      })
    }
    for (let i = 0; i < 9; i++) pipelines.push(await pipeline(i))
    sceneMS = await pipeline(0, 4)
    displayPipeline = await pipeline(8, 1, format)
    linear = device.createSampler({
      minFilter: 'linear',
      magFilter: 'linear',
      addressModeU: 'clamp-to-edge',
      addressModeV: 'clamp-to-edge',
    })
    point = device.createSampler({ minFilter: 'nearest', magFilter: 'nearest' })
    for (const [i, name, w, h] of [
      [AREA, 'area', 160, 560],
      [SEARCH, 'search', 64, 16],
    ]) {
      const r = await fetch(shaderDirectory + 'smaa-' + name + '.bin')
      if (!r.ok) throw Error('Missing SMAA table')
      const data = new Uint8Array(await r.arrayBuffer())
      if (data.length !== w * h * 4) throw Error('Invalid SMAA lookup table')
      textures[i] = texture(w, h, 'rgba8unorm')
      device.queue.writeTexture(
        { texture: textures[i] },
        data,
        { bytesPerRow: w * 4 },
        { width: w, height: h },
      )
    }
  } catch (error) {
    uniform?.destroy()
    textures.forEach((t) => t?.destroy())
    device.destroy()
    throw error
  }
  function reset() {
    frames = 0
    previousJitter = [0, 0]
  }
  function resize(w, h) {
    const ss = settings.method === 'ssaa'
    if (w === width && h === height && ss === superSample) return
    width = w
    height = h
    superSample = ss
    reset()
    for (let i = 0; i < 17; i++)
      if (i !== AREA && i !== SEARCH) {
        textures[i]?.destroy()
        const big = ss && (i === C || i === M)
        textures[i] = texture(
          i === DUMMY ? 1 : w * (big ? 2 : 1),
          i === DUMMY ? 1 : h * (big ? 2 : 1),
          'rgba16float',
          i === MC || i === MM ? 4 : 1,
        )
      }
    depth?.destroy()
    depthMS?.destroy()
    depthBaseline?.destroy()
    depth = texture(w * (ss ? 2 : 1), h * (ss ? 2 : 1), 'depth32float')
    depthMS = texture(w, h, 'depth32float', 4)
    depthBaseline = texture(w, h, 'depth32float')
    canvas.width = w
    canvas.height = h
    context.configure({ device, format, alphaMode: 'opaque' })
  }
  function plan() {
    const taa = settings.method === 'taa',
      jitter = taa
        ? [halton((frames % 8) + 1, 2) - 0.5, halton((frames % 8) + 1, 3) - 0.5]
        : [0, 0],
      prev = frames ? previous : camera,
      rad = Math.PI / 180
    const u = [
      width,
      height,
      time,
      names.indexOf(settings.method),
      ...jitter,
      ...previousJitter,
      camera.yaw * rad,
      prev.yaw * rad,
      camera.exposure,
      frames ? previousTime : time,
      frames ? 1 : 0,
      settings.historyWeight,
      settings.zoom,
      settings.split ? 1 : 0,
      camera.distance,
      prev.distance,
      camera.pitch * rad,
      prev.pitch * rad,
      1,
      1,
      0,
      0,
      0,
      settings.textureFiltering ? 1 : 0,
      0,
      0,
      0.012,
      0.02,
      0.035,
      0.1,
    ]
    const passes = [],
      add = (program, target) => {
        const p = { program, target, sources: Array(8).fill(DUMMY), u: [...u] }
        passes.push(p)
        return p
      }
    add(0, B).u.splice(4, 4, 0, 0, 0, 0)
    add(0, C).ms = settings.method === 'msaa'
    let filtered = C
    if (settings.method === 'fxaa') {
      add(2, F).sources[0] = C
      filtered = F
    }
    if (settings.method === 'ssaa') {
      add(1, F).sources[0] = C
      filtered = F
    }
    if (settings.method === 'smaa') {
      add(3, G).sources[0] = C
      add(4, E).sources[0] = G
      const p = add(5, W)
      p.sources[1] = E
      p.sources[6] = AREA
      p.sources[7] = SEARCH
      const b = add(6, F)
      b.sources[0] = C
      b.sources[2] = W
      filtered = F
    }
    if (taa) {
      filtered = frames % 2 ? HB : HA
      const p = add(7, filtered)
      p.sources[0] = C
      p.sources[3] = frames % 2 ? HA : HB
      p.sources[4] = M
      p.sources[5] = PM
      add(1, PM).sources[0] = M
    }
    const p = add(8, O)
    p.sources[0] = filtered
    p.sources[3] = B
    return { passes, jitter }
  }
  async function draw(w, h) {
    check()
    if (!Number.isInteger(w) || !Number.isInteger(h) || w < 1 || h < 1 || w > 2048 || h > 2048)
      throw Error('AA dimensions must be 1..2048')
    resize(w, h)
    const { passes, jitter } = plan(),
      encoder = device.createCommandEncoder()
    device.pushErrorScope('validation')
    for (const i of [HA, HB, PM, DUMMY])
      if (frames === 0 || i === DUMMY) {
        const p = encoder.beginRenderPass({
          colorAttachments: [
            {
              view: textures[i].createView(),
              clearValue: [0, 0, 0, 0],
              loadOp: 'clear',
              storeOp: 'store',
            },
          ],
        })
        p.end()
      }
    function render(p, index, present = false) {
      device.queue.writeBuffer(uniform, index * 256, new Float32Array(p.u))
      const scene = p.program === 0,
        target = textures[p.target]
      const colorAttachments = Array.from({ length: scene ? 2 : 1 }, (_, a) => ({
        view: present
          ? context.getCurrentTexture().createView()
          : textures[p.ms ? MC + a : p.target + a].createView(),
        ...(p.ms ? { resolveTarget: textures[p.target + a].createView() } : {}),
        clearValue: a ? [0, 0, 1, 1] : [0.012, 0.02, 0.035, 0.1],
        loadOp: 'clear',
        storeOp: p.ms ? 'discard' : 'store',
      }))
      const pass = encoder.beginRenderPass({
        colorAttachments,
        ...(scene
          ? {
              depthStencilAttachment: {
                view: (p.target === B ? depthBaseline : p.ms ? depthMS : depth).createView(),
                depthLoadOp: 'clear',
                depthClearValue: 1,
                depthStoreOp: 'discard',
              },
            }
          : {}),
      })
      pass.setPipeline(present ? displayPipeline : p.ms ? sceneMS : pipelines[p.program])
      const group = device.createBindGroup({
        layout,
        entries: Array.from({ length: 11 }, (_, i) => ({
          binding: i,
          resource:
            i === 0
              ? { buffer: uniform, size: 128 }
              : i < 9
                ? textures[p.sources[i - 1]].createView()
                : i === 9
                  ? linear
                  : point,
        })),
      })
      pass.setBindGroup(0, group, [index * 256])
      pass.draw(scene ? 79 * 6 : 3)
      pass.end()
    }
    passes.forEach((p, i) => render(p, i))
    // Display the encoded final output with a neutral copy pipeline? Display shader takes linear input,
    // so repeat its resolve directly to the canvas, rather than gamma-encoding AAOutput twice.
    render(passes.at(-1), passes.length, true)
    device.queue.submit([encoder.finish()])
    await device.queue.onSubmittedWorkDone()
    const error = await device.popErrorScope()
    if (error) throw Error(error.message)
    previous = { ...camera }
    previousTime = time
    previousJitter = jitter
    frames++
    return 'rendered'
  }
  return {
    getAntiAliasing: () => ({ ...settings }),
    setAntiAliasing(value) {
      check()
      const next = { ...settings, ...value }
      if (
        !names.includes(next.method) ||
        !Number.isFinite(next.historyWeight) ||
        next.historyWeight < 0 ||
        next.historyWeight > 0.98 ||
        !Number.isFinite(next.zoom) ||
        next.zoom < 1 ||
        next.zoom > 8 ||
        typeof next.split !== 'boolean' ||
        typeof next.textureFiltering !== 'boolean'
      )
        throw Error('Invalid AA settings')
      if (
        next.method !== settings.method ||
        next.historyWeight !== settings.historyWeight ||
        next.textureFiltering !== settings.textureFiltering
      )
        reset()
      settings = next
    },
    getCamera: () => ({ ...camera }),
    setCamera(value) {
      check()
      const c = { ...camera, ...value }
      if (
        ![c.yaw, c.pitch, c.distance, c.exposure].every(Number.isFinite) ||
        c.distance < 4 ||
        c.distance > 20 ||
        Math.abs(c.pitch) > 60 ||
        c.exposure <= 0 ||
        c.exposure > 4
      )
        throw Error('Invalid AA camera')
      if (
        Math.abs(c.yaw - camera.yaw) > 20 ||
        Math.abs(c.distance - camera.distance) > 2 ||
        c.exposure !== camera.exposure
      )
        reset()
      camera = c
    },
    setTime(value) {
      check()
      if (!Number.isFinite(value) || value < 0 || value > 1e6) throw Error('Invalid AA time')
      if (Math.abs(value - time) > 0.25) reset()
      time = value
    },
    getHistoryFrames: () => (settings.method === 'taa' ? frames : 0),
    resetHistory() {
      check()
      reset()
    },
    drawFrame(w, h) {
      check()
      const operation = tail.then(() => draw(w, h))
      tail = operation.catch(() => {})
      return operation
    },
    async readPixels() {
      check()
      await tail
      const row = Math.ceil((width * 8) / 256) * 256,
        buffer = device.createBuffer({
          size: row * height,
          usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
        }),
        e = device.createCommandEncoder()
      e.copyTextureToBuffer(
        { texture: textures[O] },
        { buffer, bytesPerRow: row },
        { width, height },
      )
      device.queue.submit([e.finish()])
      await buffer.mapAsync(GPUMapMode.READ)
      const bytes = new Uint8Array(buffer.getMappedRange()).slice()
      buffer.unmap()
      buffer.destroy()
      return { width, height, rowBytes: row, pixels: bytes }
    },
    dispose() {
      if (disposed) return
      disposed = true
      context.unconfigure()
      uniform.destroy()
      textures.forEach((t) => t?.destroy())
      depth?.destroy()
      depthMS?.destroy()
      depthBaseline?.destroy()
      device.destroy()
    },
  }
}
