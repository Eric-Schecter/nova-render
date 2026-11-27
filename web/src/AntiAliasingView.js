import {
  copyProbe,
  effectiveProbes,
  probeMetadata,
  defaultReflectionProbe,
  packProbeUniforms,
  appendProbePasses,
} from './ReflectionProbe.js'
import {
  defaultPlanarReflections,
  copyPlanarReflections,
  packPlanarUniforms,
  appendPlanarPasses,
} from './PlanarReflections.js'
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
const modelClearcoatTarget = 102
const hybridReflectionTarget = 103
const reflectionHistoryA = 104
const reflectionHistoryB = 105
const reflectionPreviousGuide = 106
const shadowHistoryA = 107,
  shadowHistoryB = shadowHistoryA + 4,
  shadowPreviousDepth = shadowHistoryB + 4,
  shadowPreviousNormal = shadowPreviousDepth + 1,
  shadowPreviousMotion = shadowPreviousNormal + 1
const hybridAO = shadowPreviousMotion + 1,
  aoHistoryA = hybridAO + 1,
  aoHistoryB = aoHistoryA + 1,
  aoPreviousGuide = aoHistoryB + 1,
  aoCombinedPosition = aoPreviousGuide + 1
const reflectionMomentsA = aoCombinedPosition + 1,
  reflectionMomentsB = reflectionMomentsA + 1,
  reflectionAtrous = reflectionMomentsB + 1
const shadowMomentsA = reflectionAtrous + 1,
  shadowMomentsB = shadowMomentsA + 4,
  shadowAtrous = shadowMomentsB + 4
const modelSpecularMapTarget = shadowAtrous + 1
const modelFilmMapTarget = modelSpecularMapTarget + 1
const hoverOutlineMask = modelFilmMapTarget + 1
const selectedOutlineMask = hoverOutlineMask + 1
const compactShadow = selectedOutlineMask + 1
const halfShadowRaw = compactShadow + 4
const lastTexture = halfShadowRaw + 3

export async function createAntiAliasingView({
  canvas,
  shaderDirectory = '/generated/',
  device: borrowedDevice,
  context: borrowedContext,
  scene: external,
  shadowScene = false,
  hdrScene = false,
  reflectionScene = false,
  planarScene = false,
  probeScene = false,
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
  let planar = defaultPlanarReflections()
  let probe = defaultReflectionProbe(),
    probeCaptures = 0,
    probeRevision = [1, 1, 1, 1],
    probeCaptured = [0, 0, 0, 0]
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
  const texture = (w, h, format = 'rgba16float', sampleCount = 1, storage = false) =>
    device.createTexture({
      size: [w, h],
      format,
      sampleCount,
      usage:
        GPUTextureUsage.RENDER_ATTACHMENT |
        GPUTextureUsage.TEXTURE_BINDING |
        (storage ? GPUTextureUsage.STORAGE_BINDING : 0) |
        (sampleCount === 1 ? GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST : 0),
    })
  let uniform, layout, pipelineLayout, linear, point
  const pipelines = new Map()
  const shaderModules = new Map()
  async function pipeline(program, count, targetFormat) {
    const key = `${program}/${count}/${targetFormat}`
    if (!pipelines.has(key)) {
      const build = async () => {
        if (!shaderModules.has(program)) {
          const load = async () => {
            const response = await fetch(shaderDirectory + `aa-${program}.json`)
            if (!response.ok) throw Error('Missing AA HLSL shader ' + program)
            const data = await response.json()
            return {
              vertex: device.createShaderModule({ code: data.vert }),
              fragment: device.createShaderModule({ code: data.frag }),
            }
          }
          shaderModules.set(
            program,
            load().catch((error) => {
              shaderModules.delete(program)
              throw error
            }),
          )
        }
        const { vertex, fragment } = await shaderModules.get(program)
        const targetCount = program === 17 ? 4 : [0, 10, 16].includes(program) ? 2 : 1
        return device.createRenderPipelineAsync({
          layout: pipelineLayout,
          vertex: { module: vertex, entryPoint: 'VSMain' },
          fragment: {
            module: fragment,
            entryPoint: 'PSMain',
            targets: Array.from({ length: targetCount }, () => ({ format: targetFormat })),
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
      pipelines.set(
        key,
        build().catch((error) => {
          pipelines.delete(key)
          throw error
        }),
      )
    }
    return pipelines.get(key)
  }
  try {
    uniform = device.createBuffer({
      size: 256 * 96,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    })
    layout = device.createBindGroupLayout({
      entries: Array.from({ length: 19 }, (_, i) => ({
        binding: i,
        visibility:
          i === 0 || i === 13
            ? GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT
            : GPUShaderStage.FRAGMENT,
        ...(i === 0
          ? { buffer: { type: 'uniform', hasDynamicOffset: true, minBindingSize: 256 } }
          : i !== 9 && i !== 10
            ? { texture: { sampleType: 'float' } }
            : { sampler: { type: i === 9 ? 'filtering' : 'non-filtering' } }),
      })),
    })
    pipelineLayout = device.createPipelineLayout({ bindGroupLayouts: [layout] })
    linear = device.createSampler({
      minFilter: 'linear',
      magFilter: 'linear',
      addressModeU: 'clamp-to-edge',
      addressModeV: 'clamp-to-edge',
    })
    point = device.createSampler({ minFilter: 'nearest', magFilter: 'nearest' })
  } catch (error) {
    uniform?.destroy()
    textures.forEach((t) => t?.destroy())
    if (!borrowedDevice) device.destroy()
    throw error
  }
  async function loadSmaaTables() {
    for (const [i, name, w, h] of [
      [AREA, 'area', 160, 560],
      [SEARCH, 'search', 64, 16],
    ]) {
      if (textures[i]) continue
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
  }
  function uploadReflectionEnvironment(data, slot = 64) {
    const next = device.createTexture({
      size: [data.width, data.height],
      format: 'rgba16float',
      usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
    })
    device.queue.writeTexture({ texture: next }, data.pixels, { bytesPerRow: data.width * 8 }, [
      data.width,
      data.height,
    ])
    textures[slot]?.destroy()
    textures[slot] = next
    if (slot === 64) probeRevision = probeRevision.map((v) => v + 1)
  }
  if (reflectionScene) uploadReflectionEnvironment(reflectionEnvironmentAtlas())
  if (probeScene) uploadReflectionEnvironment(probeMetadata(probe), 77)
  function reset() {
    frames = 0
    previousJitter = [0, 0]
  }
  function resize(w, h) {
    const ss = settings.method === 'ssaa' && (!external?.raster || external.raster.view === 'final')
    if (w === width && h === height && ss === superSample) return
    width = w
    height = h
    superSample = ss
    reset()
    for (let i = 0; i <= lastTexture; i++)
      if (i !== AREA && i !== SEARCH && i !== 38 && i !== 39 && i !== 64 && !(i >= 73 && i <= 77)) {
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
    const debug =
      (external?.raster?.view && external.raster.view !== 'final') ||
      (external?.hybrid && external.hybrid.view !== 'final')
    const halfReflections =
      external?.hybrid?.reflections && external.hybrid.halfResolutionReflections
    const halfAO =
      !!external?.hybrid?.ambientOcclusion && external.hybrid.halfResolutionAO && !debug
    const denoiseReflections = halfReflections && external.hybrid.reflectionDenoising
    const shadowGroups = Math.ceil((external?.shadowLightCount ?? 0) / 4)
    const halfHybridShadows =
      !debug && external?.hybrid?.shadows && external.hybrid.halfResolutionShadows
    const temporalShadows =
      shadowGroups > 0 &&
      !!external?.hybrid?.shadows &&
      external.hybrid.shadowTemporalReconstruction &&
      !debug
    const reconstruct = !!external?.hybrid?.temporalReconstruction && !debug
    const effectiveMethod = debug ? 'off' :
      external?.raster?.pipeline === 'deferred' && settings.method === 'msaa' ? 'fxaa' : settings.method
    const taa = effectiveMethod === 'taa',
      jitter = taa
        ? [halton((frames % 8) + 1, 2) - 0.5, halton((frames % 8) + 1, 3) - 0.5]
        : [0, 0],
      prev = frames ? previous : camera,
      rad = Math.PI / 180
    const u = [
      width,
      height,
      time,
      names.indexOf(effectiveMethod),
      ...jitter,
      ...previousJitter,
      camera.yaw * rad,
      prev.yaw * rad,
      camera.exposure,
      frames ? previousTime : time,
      frames ? 1 : 0,
      settings.historyWeight,
      settings.zoom,
      settings.split && !debug ? 1 : 0,
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
      packReflectionUniforms(u, reflections, width, height, effectiveMethod === 'ssaa')
    if (planarScene) packPlanarUniforms(u, planar)
    if (probeScene) packProbeUniforms(u, probe)
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
    if (probeScene) {
      appendProbePasses(
        passes,
        u,
        probe,
        probeRevision.map((v, i) => v !== probeCaptured[i]),
        effectiveMethod === 'msaa',
        taa,
        settings.split,
      )
    } else if (reflectionScene) {
      ;(planarScene ? appendPlanarPasses : appendReflectionPasses)(
        passes,
        u,
        planarScene ? planar : reflections,
        effectiveMethod === 'msaa',
        taa,
        settings.split,
      )
    } else if (external) {
      u[26] = external.encoded ? 2 : 1
      u.splice(28, 4, ...external.clear())
      if (settings.split && !debug) {
        add(-1, 21).u.splice(4, 4, 0, 0, 0, 0)
        crop(B, 21, width, height, true)
      }
      const oit = external.alphaBlend && !debug,
        sceneColor = oit ? 91 : 17
      if (external.raster?.pipeline === 'deferred') {
        if (denoiseReflections || temporalShadows || halfAO) add(-2, 18)
        add(-4, 85)
        if (external.hybrid || external.halfShadows) {
          const addShadows = (program, target) => {
            const shadow = add(program, target)
            shadow.sources.splice(0, 4, 85, 86, 87, 88)
            shadow.sources[8] = modelClearcoatTarget
            shadow.sources[10] = modelSpecularMapTarget
            shadow.sources[11] = modelFilmMapTarget
            return shadow
          }
          if (halfHybridShadows) {
            const coarse = addShadows(-11, compactShadow)
            coarse.u[4] *= 0.5
            coarse.u[5] *= 0.5
          }
          const fallback = addShadows(-7, halfHybridShadows ? halfShadowRaw : 92)
          const scale = superSample ? 2 : 1
          fallback.sparseShadow =
            halfHybridShadows &&
            external.hybrid.sparseShadowFallback &&
            4 * (4 + (width * scale + 2) * (height * scale + 2)) <=
              Math.min(device.limits.maxStorageBufferBindingSize, device.limits.maxBufferSize)
        }
        if (halfReflections) {
          const reflection = add(-8, hybridReflectionTarget)
          reflection.sources.splice(0, 4, 85, 86, 87, 88)
          reflection.sources[8] = modelClearcoatTarget
          reflection.sources[10] = modelSpecularMapTarget
          reflection.sources[11] = modelFilmMapTarget
          reflection.u[4] *= 0.5
          reflection.u[5] *= 0.5
        }
        let resolvedReflection = hybridReflectionTarget
        if (denoiseReflections) {
          resolvedReflection = frames % 2 ? reflectionHistoryB : reflectionHistoryA
          const previous = frames % 2 ? reflectionHistoryA : reflectionHistoryB
          const resolve = add(30, resolvedReflection)
          resolve.sources.splice(
            0,
            7,
            hybridReflectionTarget,
            86,
            85,
            previous,
            18,
            reflectionPreviousGuide,
            87,
          )
          resolve.hitReprojection = external.hybrid.reflectionHitReprojection
          resolve.u[10] = resolve.hitReprojection ? 1 : 0
          resolve.u[11] = 0
          resolve.u[19] = 0
          if (external.hybrid.clearcoatReflections) {
            resolve.sources[7] = modelClearcoatTarget
            resolve.u[8] = 1
          }
          if (external.hybrid.reflectionSVGF) {
            const moments = frames % 2 ? reflectionMomentsB : reflectionMomentsA
            const update = add(30, moments)
            update.sources = [...resolve.sources]
            update.sources[3] = frames % 2 ? reflectionMomentsA : reflectionMomentsB
            update.u = [...resolve.u]
            update.u[19] = 1
            update.hitReprojection = resolve.hitReprojection
            let source = resolvedReflection
            for (const step of [1, 2, 4]) {
              const target = step === 2 ? reflectionAtrous : hybridReflectionTarget
              const filter = add(30, target)
              filter.sources = [...resolve.sources]
              filter.sources[0] = source
              filter.sources[3] = moments
              filter.u[8] = resolve.u[8]
              filter.u[11] = 0
              filter.u[19] = step + 1
              source = target
            }
          } else {
            const spatial = add(30, hybridReflectionTarget)
            spatial.sources[0] = resolvedReflection
            spatial.sources[1] = 86
            spatial.sources[2] = 85
            spatial.sources[4] = 18
            spatial.sources[6] = 87
            spatial.u[9] = 1
            spatial.u[11] = 0
            spatial.u[19] = 0
            spatial.sources[7] = resolve.sources[7]
            spatial.u[8] = resolve.u[8]
          }
          resolvedReflection = hybridReflectionTarget
          const guide = add(30, reflectionPreviousGuide)
          guide.sources[1] = 86
          guide.sources[4] = 18
          guide.sources[6] = 87
          guide.u[27] = 1
          guide.u[11] = 0
          guide.u[19] = 0
          if (external.hybrid.clearcoatReflections) {
            guide.sources[7] = modelClearcoatTarget
            guide.u[8] = 1
          }
        }
        if (halfHybridShadows)
          for (let i = 0; i < Math.max(1, shadowGroups); ++i) {
            const p = add(30, 92 + i)
            p.sources[0] = halfShadowRaw + i
            p.sources[1] = 86
            p.sources[2] = 85
            p.sources[3] = compactShadow + i
            p.sources[6] = 87
            p.u[11] = 5
          }
        let resolvedShadows = 92
        if (temporalShadows) {
          const previousShadows = frames % 2 ? shadowHistoryA : shadowHistoryB
          resolvedShadows = frames % 2 ? shadowHistoryB : shadowHistoryA
          for (let i = 0; i < shadowGroups; i++) {
            const resolve = add(7, resolvedShadows + i)
            resolve.sources.splice(
              0,
              8,
              92 + i,
              86,
              shadowPreviousNormal,
              previousShadows + i,
              18,
              shadowPreviousMotion,
              DUMMY,
              shadowPreviousDepth,
            )
            resolve.u[8] = effectiveMethod === 'ssaa' ? 2 : 1
            resolve.u[27] = 3
            resolve.u[19] = 0
            if (external.hybrid.shadowSVGF) {
              const moments = (frames % 2 ? shadowMomentsB : shadowMomentsA) + i
              const update = add(7, moments)
              update.sources = [...resolve.sources]
              update.sources[3] = (frames % 2 ? shadowMomentsA : shadowMomentsB) + i
              update.u = [...resolve.u]
              update.u[19] = 1
              let source = resolvedShadows + i
              for (const step of [1, 2, 4]) {
                const target = step === 2 ? shadowAtrous : 92 + i
                const filter = add(7, target)
                filter.sources.splice(0, 7, source, 86, resolvedShadows + i, moments, 18, DUMMY, 87)
                filter.u[8] = resolve.u[8]
                filter.u[27] = 3
                filter.u[19] = step + 1
                source = target
              }
            }
          }
          if (external.hybrid.shadowSVGF) resolvedShadows = 92
          for (const [target, source] of [
            [shadowPreviousDepth, 87],
            [shadowPreviousNormal, 86],
            [shadowPreviousMotion, 18],
          ]) {
            const guide = add(1, target)
            guide.sources[0] = source
            guide.u[26] = 1
          }
        }
        if (halfAO) {
          const trace = add(-9, hybridAO)
          trace.sources.splice(0, 4, 85, 86, 87, 88)
          trace.sources[8] = modelClearcoatTarget
          trace.sources[10] = modelSpecularMapTarget
          trace.sources[11] = modelFilmMapTarget
          const currentAO = frames % 2 ? aoHistoryB : aoHistoryA
          const resolve = add(30, currentAO)
          resolve.sources.splice(
            0,
            7,
            hybridAO,
            86,
            DUMMY,
            frames % 2 ? aoHistoryA : aoHistoryB,
            18,
            aoPreviousGuide,
            87,
          )
          resolve.u[11] = 1
          const guide = add(30, aoPreviousGuide)
          guide.sources[1] = 86
          guide.sources[4] = 18
          guide.sources[6] = 87
          guide.u[11] = 2
          // Reuse the trace target after history resolve. Filter only the displayed AO.
          const spatial = add(30, hybridAO)
          spatial.sources[0] = currentAO
          spatial.sources[1] = 86
          spatial.sources[4] = 18
          spatial.sources[6] = 87
          spatial.u[11] = 4
          const composite = add(30, aoCombinedPosition)
          composite.sources[0] = hybridAO
          composite.sources[1] = 86
          composite.sources[2] = 85
          composite.sources[6] = 87
          composite.u[11] = 3
          composite.u[16] = external.hybrid.separateSpecularOcclusion ? 1 : 0
        }
        const p = add(-5, sceneColor)
        if (halfReflections) p.sources[9] = resolvedReflection
        if (external.hybrid || external.halfShadows)
          for (let i = 0; i < 4; i++)
            p.sources[4 + i] = (i < shadowGroups ? resolvedShadows : 92) + i
        p.sources.splice(0, 4, 85, 86, halfAO ? aoCombinedPosition : 87, 88)
        p.sources[8] = modelClearcoatTarget
        p.sources[10] = modelSpecularMapTarget
        p.sources[11] = modelFilmMapTarget
      } else add(-1, sceneColor).ms = effectiveMethod === 'msaa'
      if (oit) {
        add(-6, 89).sources[0] = 91
        if (!reconstruct) add(29, 17).sources.splice(0, 3, 91, 89, 90)
      }
      const scale = effectiveMethod === 'ssaa' ? 2 : 1
      crop(C, external.alphaBlend && reconstruct ? 91 : 17, width * scale, height * scale, true)
    } else if (hdrScene) {
      if (settings.split && !debug) {
        const p = add(16, B)
        p.u.splice(4, 4, 0, 0, 0, 0)
      }
      add(16, C).ms = effectiveMethod === 'msaa'
    } else if (shadowScene) {
      const shadowed = shadowSettings.method !== 'off'
      if (shadowed) add(9, 22).u.splice(28, 4, 1, 0, 0, 1)
      if (settings.split && !debug) {
        const p = add(10, B)
        p.u.splice(4, 4, 0, 0, 0, 0)
        if (shadowed) p.sources[0] = 22
      }
      const p = add(10, C)
      p.ms = effectiveMethod === 'msaa'
      if (shadowed) p.sources[0] = 22
    } else {
      add(0, B).u.splice(4, 4, 0, 0, 0, 0)
      add(0, C).ms = effectiveMethod === 'msaa'
    }
    let filtered = C
    if (!external && !reflectionScene && effectiveMethod === 'ssaa') {
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
    if (reconstruct) {
      if (!denoiseReflections && !temporalShadows && !halfAO) add(-2, 18)
      const scale = effectiveMethod === 'ssaa' ? 2 : 1
      crop(M, 18, width * scale, height * scale, false)
      crop(98, 86, width * scale, height * scale, false)
      crop(99, 87, width * scale, height * scale, false)
      filtered = frames % 2 ? HB : HA
      const p = add(7, filtered)
      p.sources.splice(0, 8, C, 98, 96, frames % 2 ? HA : HB, M, PM, 99, 97)
      p.u[27] = 2
      p.u[13] = external.hybrid.historyWeight
      add(1, PM).sources[0] = M
      add(1, 96).sources[0] = 98
      add(1, 97).sources[0] = 99
      if (external.alphaBlend) {
        crop(100, 89, width * scale, height * scale, false)
        crop(101, 90, width * scale, height * scale, false)
        add(29, C).sources.splice(0, 3, filtered, 100, 101)
        filtered = C
      }
    }
    const baselineSource =
      (!shadowScene && !hdrScene && !reflectionScene) || settings.split ? B : DUMMY
    const modelCoverage = external?.raster?.pipeline === 'deferred' &&
      external.solidBackground && !postSettings.toneMapBackground && !external.alphaBlend &&
      !settings.split && !reconstruct && !taa ? 85 : DUMMY
    const post = appendPostProcessing(
      passes,
      filtered,
      baselineSource,
      width,
      height,
      debug ||
        (reflectionScene && reflectionDebugModes.indexOf(reflections.debug) >= 2) ||
        (probeScene && probe.debug === 'influence')
        ? { ...postSettings, enabled: false }
        : postSettings,
      postState,
      u,
      modelCoverage,
      effectiveMethod === 'ssaa' ? 2 : 1,
    )
    filtered = post.color
    if (effectiveMethod === 'fxaa') {
      add(2, F).sources[0] = filtered
      filtered = F
    }
    if (effectiveMethod === 'smaa') {
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
    const outlines = !debug ? external?.outlines : null
    const scale = effectiveMethod === 'ssaa' ? 2 : 1
    if (outlines?.hoveredId) {
      add(-4, 85).outlineGroup = 1
      crop(hoverOutlineMask, 85, width * scale, height * scale, false)
    }
    if (outlines?.selectedIds.length) {
      add(-4, 85).outlineGroup = 2
      crop(selectedOutlineMask, 85, width * scale, height * scale, false)
    }
    const p = add(8, O)
    if (outlines?.hoveredId || outlines?.selectedIds.length) p.u[27] = 5
    if (outlines?.hoveredId) {
      p.sources[1] = hoverOutlineMask
      p.u.splice(48, 4, ...outlines.hovered.color, outlines.hovered.width)
    }
    if (outlines?.selectedIds.length) {
      p.sources[2] = selectedOutlineMask
      p.u.splice(52, 4, ...outlines.selected.color, outlines.selected.width)
    }
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
    const captureRevision = [...probeRevision],
      capturing = passes.filter((p) => p.program === 21).map((p) => p.target - 73)
    const used = new Set([DUMMY])
    const sparse = passes.some((pass) => pass.sparseShadow)
    for (const p of passes) {
      used.add(p.target)
      p.sources.forEach((i) => used.add(i))
      if (p.program === -6) used.add(p.target + 1)
      if (p.program === -4) {
        used.add(modelClearcoatTarget)
        used.add(modelSpecularMapTarget)
        used.add(modelFilmMapTarget)
      }
      if (p.program === 17 || p.program === -4 || p.program === -7 || p.program === -11) {
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
    for (let i = 0; i <= lastTexture; i++) {
      if (i === AREA || i === SEARCH || i === 64 || i === 77) continue
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
      if ((i >= 42 && i < 64) || i >= 65) [tw, th] = reflectionResourceSize(i, w, h, superSample)
      if (i === hoverOutlineMask || i === selectedOutlineMask) [tw, th] = [w, h]
      if (i >= compactShadow && i < halfShadowRaw) {
        const scale = superSample ? 2 : 1
        tw = Math.ceil((w * scale) / 2) + 2
        th = Math.ceil((h * scale) / 2) + 2
      }
      if ((i >= 85 && i < 96) || i === modelClearcoatTarget || i >= halfShadowRaw) {
        const scale = superSample ? 2 : 1
        tw = w * scale + 2
        th = h * scale + 2
      }
      if (external?.halfShadows && i >= 92 && i <= 95) {
        tw = Math.ceil((tw - 2) / 2) + 2
        th = Math.ceil((th - 2) / 2) + 2
      }
      if (i >= 96 && i !== modelClearcoatTarget) {
        tw = w
        th = h
      }
      if (
        (i >= hybridAO && i < aoCombinedPosition) ||
        (i >= reflectionMomentsA && i <= reflectionAtrous)
      ) {
        const scale = superSample ? 2 : 1
        tw = Math.ceil((w * scale) / 2) + 2
        th = Math.ceil((h * scale) / 2) + 2
      } else if (i >= shadowHistoryA) {
        const scale = superSample ? 2 : 1
        tw = w * scale + 2
        th = h * scale + 2
      } else if (i >= hybridReflectionTarget) {
        const scale = superSample ? 2 : 1
        tw = Math.ceil((w * scale) / 2) + 2
        th = Math.ceil((h * scale) / 2) + 2
      }
      const storage = sparse && i >= halfShadowRaw
      if (
        textures[i] &&
        (textures[i].width !== tw ||
          textures[i].height !== th ||
          Boolean(textures[i].usage & GPUTextureUsage.STORAGE_BINDING) !== storage)
      ) {
        textures[i].destroy()
        textures[i] = undefined
      }
      if (!textures[i])
        textures[i] = texture(
          tw,
          th,
          'rgba16float',
          [MC, MM, 19, 20, 46, 47, 48, 49].includes(i) ? 4 : 1,
          storage,
        )
    }
    // With temporal OIT, only the opaque raw target is retained by the graph.
    const raw = reflectionScene
      ? textures[42]
      : external
        ? textures[17] || textures[91]
        : textures[C]
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
    const [displayPipeline] = await Promise.all([
      pipeline(passes.at(-1).program, 1, format),
      ...passes
        .filter((pass) => pass.program >= 0)
        .map(async (pass) => {
          pass.pipeline = await pipeline(pass.program, pass.ms ? 4 : 1, 'rgba16float')
        }),
      used.has(AREA) ? loadSmaaTables() : undefined,
      external?.prepare(w, h, settings, frames, passes),
    ])
    check()
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
      if (p.sparseShadow) {
        external.sparseShadows(
          encoder,
          p.sources.slice(0, 3).map((i) => textures[i]),
          Array.from({ length: 4 }, (_, i) => textures[p.target + i]),
        )
        return
      }
      if (p.hitReprojection) external.writeReflectionReprojection(p.u)
      device.queue.writeBuffer(uniform, index * 256, new Float32Array(p.u))
      const scene =
          (p.program <= 0 &&
            p.program !== -8 &&
            p.program !== -9 &&
            p.program !== -11 &&
            !(p.program === -7 && external?.halfShadows)) ||
          p.program === 9 ||
          p.program === 10 ||
          p.program === 16 ||
          p.program === 17,
        native = p.program < 0,
        target = textures[p.target]
      const colorAttachments = Array.from(
        {
          length:
            p.program === -4
              ? 7
              : p.program === 17 || p.program === -7 || p.program === -11
                ? 4
                : p.program === -6 || p.program === 0 || p.program === 10 || p.program === 16
                  ? 2
                  : 1,
        },
        (_, a) => ({
          view: present
            ? context.getCurrentTexture().createView()
            : textures[
                p.ms
                  ? (p.program === 17 ? 46 : native ? 19 : MC) + a
                  : p.program === -4 && a === 6
                    ? modelFilmMapTarget
                  : p.program === -4 && a === 5
                    ? modelSpecularMapTarget
                  : p.program === -4 && a === 4
                    ? modelClearcoatTarget
                    : p.target + a
              ].createView(),
          ...(p.ms ? { resolveTarget: textures[p.target + a].createView() } : {}),
          clearValue:
            p.program === -6
              ? a
                ? [1, 1, 1, 1]
                : [0, 0, 0, 0]
              : p.program === -4
                ? [0, 0, 0, 0]
                : p.program === 17 && a === 3
                  ? [1000, 1000, 0, 1]
                  : p.program === 17 && a === 2
                    ? [0, 0, 0, 1]
                    : p.program === -2 || p.program === -3 || a
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
                depthLoadOp:
                  p.program === -5 || p.program === -6 || p.program === -7 ? 'load' : 'clear',
                depthClearValue: 1,
                depthStoreOp: 'store',
              },
            }
          : {}),
      })
      if (native) {
        pass.setViewport(1 + p.u[4], 1 + p.u[5], target.width - 2, target.height - 2, 0, 1)
        if (p.program !== -3)
          external.draw(
            pass,
            p.program === -2,
            p.ms ? 4 : 1,
            p.program,
            p.program === -5 ||
              p.program === -6 ||
              p.program === -7 ||
              p.program === -8 ||
              p.program === -9 ||
              p.program === -11
              ? p.sources.slice(0, 12).map((i) => textures[i])
              : null,
            p.outlineGroup || 0,
          )
        pass.end()
        return
      }
      pass.setPipeline(present ? displayPipeline : p.pipeline)
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
    if (probeScene)
      for (const index of capturing) {
        probeCaptured[index] = captureRevision[index]
        probeCaptures++
      }
    external?.finish()
    previous = { ...camera }
    previousTime = time
    previousJitter = jitter
    frames++
    return 'rendered'
  }
  return {
    getReflectionProbe: () => copyProbe(probe),
    setReflectionProbe(value) {
      const next = copyProbe({ ...probe, ...value }),
        before = effectiveProbes(probe),
        after = effectiveProbes(next)
      after.forEach((v, i) => {
        if (!before[i] || JSON.stringify(v.position) !== JSON.stringify(before[i].position))
          probeRevision[i]++
      })
      if (JSON.stringify(before) !== JSON.stringify(after))
        uploadReflectionEnvironment(probeMetadata(next), 77)
      if (JSON.stringify(next) !== JSON.stringify(probe)) reset()
      probe = next
    },
    setReflectionProbes(volumes) {
      this.setReflectionProbe({
        probes: volumes,
        debugProbe: Math.min(probe.debugProbe, Math.max(0, volumes.length - 1)),
      })
    },
    getReflectionProbes: () => effectiveProbes(copyProbe(probe)),
    updateProbe(index) {
      if (
        index !== undefined &&
        (!Number.isInteger(index) || index < 0 || index >= effectiveProbes(probe).length)
      )
        throw Error('Probe index out of range')
      if (index === undefined) probeRevision = probeRevision.map((v) => v + 1)
      else probeRevision[index]++
      reset()
    },
    getProbeCaptureCount: () => probeCaptures,
    getPlanarReflections: () => ({ ...planar }),
    setPlanarReflections(value) {
      if (!planarScene) throw Error('Not a planar reflection view')
      const next = copyPlanarReflections({ ...planar, ...value })
      if (JSON.stringify(next) !== JSON.stringify(planar)) reset()
      planar = next
    },
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
    getHistoryFrames: () => frames,
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
