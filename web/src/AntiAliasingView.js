import {
  defaultScreenSpaceReflections,
  copyScreenSpaceReflections,
  reflectionEnvironmentAtlas,
  loadEnvironmentFile,
  reflectionResourceSize,
  packReflectionUniforms,
  appendReflectionPasses,
  reflectionDebugModes,
} from './ScreenSpaceReflections.js'
import {
  defaultPostProcessing,
  copyPostProcessing,
  appendPostProcessing,
  postResourceSize,
} from './PostProcessing.js'
import { defaultShadowSettings, copyShadowSettings } from './ShadowSettings.js'
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
export async function createAntiAliasingView({
  canvas,
  shaderDirectory = '/generated/',
  device: borrowedDevice,
  context: borrowedContext,
  scene: external,
  shadowScene = false,
  hdrScene = false,
  reflectionScene = false,
} = {}) {
  if (!canvas) throw Error('AA requires a canvas')
  const adapter = borrowedDevice ? null : await navigator.gpu?.requestAdapter()
  if (!borrowedDevice && !adapter) throw Error('WebGPU unavailable')
  const device = borrowedDevice || (await adapter.requestDevice()),
    context = borrowedContext || canvas.getContext('webgpu')
  if (!context) {
    if (!borrowedDevice) device.destroy()
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
    depthBaseline,
    depthShadow
  let settings = {
      method: external ? 'off' : 'taa',
      historyWeight: 0.9,
      zoom: 1,
      split: !external && !shadowScene && !hdrScene && !reflectionScene,
      textureFiltering: true,
    },
    camera = { yaw: 20, pitch: 15, distance: 6.2, exposure: 1 },
    previous = { ...camera },
    time = 0,
    previousTime = 0,
    previousJitter = [0, 0]
  if (shadowScene) {
    camera = { ...camera, yaw: 28, pitch: 30, distance: 8 }
    previous = { ...camera }
  }
  if (hdrScene) {
    camera = { yaw: 0, pitch: 0, distance: 6.2, exposure: 1 }
    previous = { ...camera }
  }
  if (reflectionScene) {
    camera = { yaw: 18, pitch: 22, distance: 8, exposure: 1 }
    previous = { ...camera }
  }
  let reflections = defaultScreenSpaceReflections()
  let postSettings = { ...defaultPostProcessing(), enabled: hdrScene || reflectionScene },
    postState = { frames: 0, last: 0 },
    sceneIntensity = 1
  let shadowSettings = defaultShadowSettings()
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
    shadowMS,
    hdrMS,
    ssrMS,
    displayPipeline,
    linear,
    point
  try {
    uniform = device.createBuffer({
      size: 256 * 64,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    })
    layout = device.createBindGroupLayout({
      entries: Array.from({ length: 19 }, (_, i) => ({
        binding: i,
        visibility:
          i === 0 ? GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT : GPUShaderStage.FRAGMENT,
        ...(i === 0
          ? { buffer: { type: 'uniform', hasDynamicOffset: true, minBindingSize: 256 } }
          : i !== 9 && i !== 10
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
          targets: Array.from(
            {
              length:
                program === 17 ? 4 : program === 0 || program === 10 || program === 16 ? 2 : 1,
            },
            () => ({
              format: targetFormat,
            }),
          ),
        },
        primitive: { topology: 'triangle-list', cullMode: 'none' },
        multisample: { count },
        ...([0, 9, 10, 16, 17].includes(program)
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
    for (let i = 0; i < 21; i++) pipelines.push(await pipeline(i))
    sceneMS = await pipeline(0, 4)
    if (shadowScene) shadowMS = await pipeline(10, 4)
    if (hdrScene) hdrMS = await pipeline(16, 4)
    if (reflectionScene) ssrMS = await pipeline(17, 4)
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
    if (!borrowedDevice) device.destroy()
    throw error
  }
  function uploadReflectionEnvironment(data) {
    const next = device.createTexture({
      size: [256, 256],
      format: 'rgba16float',
      usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
    })
    device.queue.writeTexture({ texture: next }, data.pixels, { bytesPerRow: 256 * 8 }, [256, 256])
    textures[64]?.destroy()
    textures[64] = next
  }
  if (reflectionScene) uploadReflectionEnvironment(reflectionEnvironmentAtlas())
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
    for (let i = 0; i < 65; i++)
      if (i !== AREA && i !== SEARCH && i !== 38 && i !== 39 && i !== 64) {
        textures[i]?.destroy()
        textures[i] = undefined
      }
    depth?.destroy()
    depthMS?.destroy()
    depthBaseline?.destroy()
    depthShadow?.destroy()
    depthShadow = undefined
    depth = depthMS = depthBaseline = undefined
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
    u.push(...Array(32).fill(0))
    if (shadowScene) {
      const sh = shadowSettings
      u.splice(
        32,
        4,
        ['off', 'hard', 'pcf', 'pcss'].indexOf(sh.method),
        sh.resolution,
        sh.depthBias,
        sh.normalBias,
      )
      u.splice(36, 4, ...sh.lightDirection, sh.lightSize)
      u.splice(40, 4, sh.filterRadius, 0, 6 + 19 * 36 + 3 * 16 * 24 * 6, 0)
      u.splice(28, 4, 0.025, 0.035, 0.055, 1)
    }
    if (hdrScene) {
      u[42] = 6 + 19 * 36 + 3 * 16 * 24 * 6
      u[58] = sceneIntensity
      u.splice(28, 4, 0.002, 0.003, 0.005, 1)
    }
    if (reflectionScene)
      packReflectionUniforms(u, reflections, width, height, settings.method === 'ssaa')
    if (postSettings.enabled || hdrScene || reflectionScene) u[26] = 1
    const passes = [],
      add = (program, target) => {
        const p = { program, target, sources: Array(16).fill(DUMMY), u: [...u] }
        passes.push(p)
        return p
      }
    const crop = (target, source, aw, ah, luma) => {
      const p = add(1, target)
      p.sources[0] = source
      p.u.splice(20, 4, aw / (aw + 2), ah / (ah + 2), 1 / (aw + 2), 1 / (ah + 2))
      p.u[24] = luma ? 1 : 0
      if (!luma) p.u[26] = 1
    }
    if (reflectionScene) {
      appendReflectionPasses(
        passes,
        u,
        reflections,
        settings.method === 'msaa',
        taa,
        settings.split,
      )
    } else if (external) {
      u[26] = external.encoded ? 2 : 1
      u.splice(28, 4, ...external.clear())
      if (settings.split) {
        add(-1, 21).u.splice(4, 4, 0, 0, 0, 0)
        crop(B, 21, width, height, true)
      }
      add(-1, 17).ms = settings.method === 'msaa'
      const scale = settings.method === 'ssaa' ? 2 : 1
      crop(C, 17, width * scale, height * scale, true)
    } else if (hdrScene) {
      if (settings.split) {
        const p = add(16, B)
        p.u.splice(4, 4, 0, 0, 0, 0)
      }
      add(16, C).ms = settings.method === 'msaa'
    } else if (shadowScene) {
      const shadowed = shadowSettings.method !== 'off'
      if (shadowed) add(9, 22).u.splice(28, 4, 1, 0, 0, 1)
      if (settings.split) {
        const p = add(10, B)
        p.u.splice(4, 4, 0, 0, 0, 0)
        if (shadowed) p.sources[0] = 22
      }
      const p = add(10, C)
      p.ms = settings.method === 'msaa'
      if (shadowed) p.sources[0] = 22
    } else {
      add(0, B).u.splice(4, 4, 0, 0, 0, 0)
      add(0, C).ms = settings.method === 'msaa'
    }
    let filtered = C
    if (!external && !reflectionScene && settings.method === 'ssaa') {
      add(1, F).sources[0] = C
      filtered = F
    }
    if (taa) {
      if (external) {
        add(external.encoded ? -3 : -2, 18)
        crop(M, 18, width, height, false)
      }
      filtered = frames % 2 ? HB : HA
      const p = add(7, filtered)
      p.sources[0] = C
      p.sources[3] = frames % 2 ? HA : HB
      p.sources[4] = M
      p.sources[5] = PM
      add(1, PM).sources[0] = M
    }
    const baselineSource =
      (!shadowScene && !hdrScene && !reflectionScene) || settings.split ? B : DUMMY
    const post = appendPostProcessing(
      passes,
      filtered,
      baselineSource,
      width,
      height,
      reflectionScene && reflectionDebugModes.indexOf(reflections.debug) >= 2
        ? { ...postSettings, enabled: false }
        : postSettings,
      postState,
      u,
    )
    filtered = post.color
    if (settings.method === 'fxaa') {
      add(2, F).sources[0] = filtered
      filtered = F
    }
    if (settings.method === 'smaa') {
      add(3, G).sources[0] = filtered
      add(4, E).sources[0] = G
      const p = add(5, W)
      p.sources[1] = E
      p.sources[6] = AREA
      p.sources[7] = SEARCH
      const b = add(6, F)
      b.sources[0] = filtered
      b.sources[2] = W
      filtered = F
    }
    const p = add(8, O)
    p.sources[0] = filtered
    if ((!shadowScene && !hdrScene && !reflectionScene) || settings.split)
      p.sources[3] = post.baseline
    return { passes, jitter }
  }
  async function draw(w, h) {
    check()
    if (
      !Number.isInteger(w) ||
      !Number.isInteger(h) ||
      w < 1 ||
      h < 1 ||
      w > (external ? 4096 : 2048) ||
      h > (external ? 4096 : 2048)
    )
      throw Error('AA dimensions exceed the view limit')
    resize(w, h)
    const { passes, jitter } = plan()
    const used = new Set([DUMMY])
    for (const p of passes) {
      used.add(p.target)
      p.sources.forEach((i) => used.add(i))
      if (p.program === 17) {
        for (let a = 1; a < 4; a++) used.add(p.target + a)
      }
      if (p.program === 0 || p.program === 10 || p.program === 16) used.add(p.target + 1)
      if (p.ms) {
        used.add(p.program === 17 ? 46 : external ? 19 : MC)
        if (p.program === 17) {
          for (let a = 1; a < 4; a++) used.add(46 + a)
        }
        if (!external) used.add(MM)
      }
    }
    for (let i = 0; i < 65; i++) {
      if (i === AREA || i === SEARCH || i === 64) continue
      if (!used.has(i)) {
        textures[i]?.destroy()
        textures[i] = undefined
        continue
      }
      const big =
        superSample && !reflectionScene && (external ? i === 17 || i === 18 : i === C || i === M)
      const raw = external && i >= 17 && i <= 21
      let tw =
          i === 22
            ? shadowSettings.resolution
            : i === DUMMY
              ? 1
              : w * (big ? 2 : 1) + (raw ? 2 : 0),
        th =
          i === 22 ? shadowSettings.resolution : i === DUMMY ? 1 : h * (big ? 2 : 1) + (raw ? 2 : 0)
      if (i >= 23) [tw, th] = postResourceSize(i, w, h)
      if (i >= 42 && i < 64) [tw, th] = reflectionResourceSize(i, w, h, superSample)
      if (textures[i] && (textures[i].width !== tw || textures[i].height !== th)) {
        textures[i].destroy()
        textures[i] = undefined
      }
      if (!textures[i])
        textures[i] = texture(
          tw,
          th,
          'rgba16float',
          [MC, MM, 19, 20, 46, 47, 48, 49].includes(i) ? 4 : 1,
        )
    }
    const raw = textures[reflectionScene ? 42 : external ? 17 : C]
    if (!depth || depth.width !== raw.width || depth.height !== raw.height) {
      depth?.destroy()
      depth = texture(raw.width, raw.height, 'depth32float')
    }
    if (
      settings.method === 'msaa' &&
      (!depthMS || depthMS.width !== raw.width || depthMS.height !== raw.height)
    ) {
      depthMS?.destroy()
      depthMS = texture(raw.width, raw.height, 'depth32float', 4)
    }
    if (used.has(B) && !depthBaseline)
      depthBaseline = texture(w + (external ? 2 : 0), h + (external ? 2 : 0), 'depth32float')
    if (
      shadowScene &&
      shadowSettings.method !== 'off' &&
      (!depthShadow || depthShadow.width !== shadowSettings.resolution)
    ) {
      depthShadow?.destroy()
      depthShadow = texture(shadowSettings.resolution, shadowSettings.resolution, 'depth32float')
    }
    if (shadowSettings.method === 'off') {
      depthShadow?.destroy()
      depthShadow = undefined
    }
    if (external) await external.prepare(w, h, settings, frames)
    const encoder = device.createCommandEncoder()
    device.pushErrorScope('validation')
    for (const i of [HA, HB, PM, DUMMY])
      if (textures[i] && (frames === 0 || i === DUMMY)) {
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
      const scene =
          p.program <= 0 ||
          p.program === 9 ||
          p.program === 10 ||
          p.program === 16 ||
          p.program === 17,
        native = p.program < 0,
        target = textures[p.target]
      const colorAttachments = Array.from(
        {
          length:
            p.program === 17 ? 4 : p.program === 0 || p.program === 10 || p.program === 16 ? 2 : 1,
        },
        (_, a) => ({
          view: present
            ? context.getCurrentTexture().createView()
            : textures[
                p.ms ? (p.program === 17 ? 46 : native ? 19 : MC) + a : p.target + a
              ].createView(),
          ...(p.ms ? { resolveTarget: textures[p.target + a].createView() } : {}),
          clearValue:
            p.program === 17 && a === 3
              ? [1000, 1000, 0, 1]
              : p.program === 17 && a === 2
                ? [0, 0, 0, 1]
                : p.program < -1 || a
                  ? [0, 0, 1, 1]
                  : p.u.slice(28, 32),
          loadOp: 'clear',
          storeOp: p.ms ? 'discard' : 'store',
        }),
      )
      const pass = encoder.beginRenderPass({
        colorAttachments,
        ...(scene
          ? {
              depthStencilAttachment: {
                view: (p.program === 9
                  ? depthShadow
                  : p.target === B || p.target === 21
                    ? depthBaseline
                    : p.ms
                      ? depthMS
                      : depth
                ).createView(),
                depthLoadOp: 'clear',
                depthClearValue: 1,
                depthStoreOp: 'discard',
              },
            }
          : {}),
      })
      if (native) {
        pass.setViewport(1 + p.u[4], 1 + p.u[5], target.width - 2, target.height - 2, 0, 1)
        if (p.program !== -3) external.draw(pass, p.program === -2, p.ms ? 4 : 1)
        pass.end()
        return
      }
      pass.setPipeline(
        present
          ? displayPipeline
          : p.ms
            ? p.program === 17
              ? ssrMS
              : p.program === 16
                ? hdrMS
                : p.program === 10
                  ? shadowMS
                  : sceneMS
            : pipelines[p.program],
      )
      const group = device.createBindGroup({
        layout,
        entries: Array.from({ length: 19 }, (_, i) => ({
          binding: i,
          resource:
            i === 0
              ? { buffer: uniform, size: 256 }
              : i !== 9 && i !== 10
                ? textures[p.sources[i < 9 ? i - 1 : i - 3]].createView()
                : i === 9
                  ? linear
                  : point,
        })),
      })
      pass.setBindGroup(0, group, [index * 256])
      pass.draw(
        p.program === 9 || p.program === 10 || p.program === 16 || p.program === 17
          ? p.u[42]
          : scene
            ? 79 * 6
            : 3,
      )
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
    external?.finish()
    previous = { ...camera }
    previousTime = time
    previousJitter = jitter
    frames++
    return 'rendered'
  }
  return {
    getScreenSpaceReflections: () => ({ ...reflections }),
    setScreenSpaceReflections(value) {
      check()
      const next = copyScreenSpaceReflections({ ...reflections, ...value })
      if (JSON.stringify(next) !== JSON.stringify(reflections)) reset()
      reflections = next
    },
    loadEnvironment(url) {
      check()
      const op = tail.then(async () => {
        const env = await loadEnvironmentFile(url),
          data = reflectionEnvironmentAtlas(env)
        check()
        await device.queue.onSubmittedWorkDone()
        uploadReflectionEnvironment(data)
        reset()
      })
      tail = op.catch(() => {})
      return op
    },
    useDefaultEnvironment() {
      check()
      uploadReflectionEnvironment(reflectionEnvironmentAtlas())
      reset()
    },
    getPostProcessing: () => ({ ...postSettings }),
    setPostProcessing(value) {
      check()
      const next = copyPostProcessing({ ...postSettings, ...value })
      if (next.enabled !== postSettings.enabled || next.autoExposure !== postSettings.autoExposure)
        postState = { frames: 0, last: 0 }
      postSettings = next
    },
    getSceneIntensity: () => sceneIntensity,
    setSceneIntensity(value) {
      check()
      if (!Number.isFinite(value) || value < 0.05 || value > 20)
        throw Error('Invalid scene intensity')
      if (value !== sceneIntensity) reset()
      sceneIntensity = value
    },
    getShadowSettings: () => copyShadowSettings(shadowSettings),
    setShadowSettings(value) {
      check()
      const next = copyShadowSettings({ ...shadowSettings, ...value })
      if (JSON.stringify(next) !== JSON.stringify(shadowSettings)) reset()
      shadowSettings = next
    },
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
      if (!borrowedContext) context.unconfigure()
      uniform.destroy()
      textures.forEach((t) => t?.destroy())
      depth?.destroy()
      depthMS?.destroy()
      depthBaseline?.destroy()
      depthShadow?.destroy()
      if (!borrowedDevice) device.destroy()
    },
  }
}
