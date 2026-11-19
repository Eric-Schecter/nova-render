import { lightRecords } from './Lighting.js'
import { copyAmbientOcclusion } from './AmbientOcclusion.js'
import { buildLightSampling } from './LightSampling.js'
import { buildBVH } from './BVH.js'
import { sceneUniforms } from './ModelData.js'
import { fallbackEnvironment } from './EnvironmentData.js'
export const defaultPathTracing = () => ({
  samplesPerFrame: 1,
  maxBounces: 3,
  resolutionScale: 0.5,
  denoise: false,
  lightSampling: true,
  integrator: 'path',
  photonsPerIteration: 8192,
  photonRadius: 0.16,
  mltBootstrapSamples: 262144,
  mltLargeStepProbability: 0.3,
  mltMutationScale: 0.01,
  mltSeed: 1,
})
export function validatePathTracing(value) {
  if (value && Object.keys(value).some((key) => !Object.hasOwn(defaultPathTracing(), key)))
    throw Error('Unknown ray tracing setting')
  if (
    !value ||
    (value.mltBootstrapSamples !== undefined &&
      (!Number.isInteger(value.mltBootstrapSamples) ||
        value.mltBootstrapSamples < 256 ||
        value.mltBootstrapSamples > 1048576)) ||
    (value.mltLargeStepProbability !== undefined &&
      (!Number.isFinite(value.mltLargeStepProbability) ||
        value.mltLargeStepProbability < 0.05 ||
        value.mltLargeStepProbability > 1)) ||
    (value.mltMutationScale !== undefined &&
      (!Number.isFinite(value.mltMutationScale) ||
        value.mltMutationScale < 0.0001 ||
        value.mltMutationScale > 0.25)) ||
    (value.mltSeed !== undefined &&
      (!Number.isInteger(value.mltSeed) || value.mltSeed < 0 || value.mltSeed > 4294967295)) ||
    (value.integrator !== undefined &&
      !['path', 'bidirectional', 'photon-mapping', 'mlt'].includes(value.integrator)) ||
    (value.lightSampling !== undefined && typeof value.lightSampling !== 'boolean') ||
    (value.denoise !== undefined && typeof value.denoise !== 'boolean') ||
    (value.photonsPerIteration !== undefined &&
      (!Number.isInteger(value.photonsPerIteration) ||
        value.photonsPerIteration < 256 ||
        value.photonsPerIteration > 65536)) ||
    (value.photonRadius !== undefined &&
      (!Number.isFinite(value.photonRadius) ||
        value.photonRadius < 0.03 ||
        value.photonRadius > 0.5)) ||
    !Number.isInteger(value.samplesPerFrame) ||
    value.samplesPerFrame < 1 ||
    value.samplesPerFrame > 4 ||
    !Number.isInteger(value.maxBounces) ||
    value.maxBounces < 1 ||
    value.maxBounces > 8 ||
    !Number.isFinite(value.resolutionScale) ||
    value.resolutionScale < 0.25 ||
    value.resolutionScale > 1
  )
    throw Error('Path tracing requires samples 1..4, bounces 1..8 and scale 0.25..1')
}
export async function createPathView(device, data, shaderDirectory, { linearOutput = false } = {}) {
  const resources = new Set()
  let guides,
    accumulation,
    output,
    computeGroup,
    presentGroup,
    scratchSize = 0,
    width = 0,
    height = 0,
    samples = 0,
    previous = '',
    skySource,
    skyBuffer,
    disposed = false
  function buffer(bytes, usage = GPUBufferUsage.STORAGE) {
    if (bytes.byteLength > device.limits.maxStorageBufferBindingSize)
      throw Error('Path scene exceeds WebGPU storage buffer limit')
    const b = device.createBuffer({
      size: Math.max(16, bytes.byteLength),
      usage: usage | GPUBufferUsage.COPY_DST,
    })
    resources.add(b)
    if (bytes.byteLength) device.queue.writeBuffer(b, 0, bytes)
    return b
  }
  function release(b) {
    if (b) {
      b.destroy()
      resources.delete(b)
    }
  }
  try {
    const bvh = buildBVH(data.vertices)
    const nodes = buffer(bvh.nodes),
      vertices = buffer(bvh.vertices),
      params = buffer(new Uint8Array(1552))
    const metadata = [],
      images = []
    const materialHeader = new Float32Array((data.materialRecords?.length || 0) / 3)
    for (let i = 0; i < materialHeader.length / 16; i++)
      materialHeader.set(data.materialRecords.subarray(i * 48 + 32, i * 48 + 48), i * 16)
    let total = materialHeader.byteLength
    for (const image of data.images.slice(0, 5)) {
      let pixels = image.pixels
      if (!pixels) {
        const canvas = new OffscreenCanvas(image.width, image.height),
          context = canvas.getContext('2d', { willReadFrequently: true })
        context.drawImage(image.bitmap, 0, 0)
        pixels = context.getImageData(0, 0, image.width, image.height).data
      }
      metadata.push(total / 4, image.width, image.height, 0)
      images.push(pixels)
      total += pixels.byteLength
    }
    const lights = buildLightSampling(data, images, bvh.vertices, bvh.originalIds)
    metadata[3] = total / 4
    // Distribution offsets are absolute words in the existing material buffer.
    for (const i of [1, 2, 3]) lights.words[i] += total / 4
    const texels = new Uint8Array(total + lights.words.byteLength)
    texels.set(new Uint8Array(materialHeader.buffer))
    let offset = materialHeader.byteLength
    for (const bytes of images) {
      texels.set(bytes, offset)
      offset += bytes.byteLength
    }
    texels.set(new Uint8Array(lights.words.buffer), total)
    const material = buffer(texels)
    async function shader(name) {
      const response = await fetch(shaderDirectory.replace(/\/?$/, '/') + name + '.json')
      if (!response.ok) throw Error('Missing path shader package: ' + name)
      return response.json()
    }
    const [
      kernel,
      presentation,
      filter,
      clearPhotons,
      tracePhotons,
      bootstrapMLT,
      reduceMLT,
      mutateMLT,
    ] = await Promise.all([
      shader('pathtrace'),
      shader('pathpresent'),
      shader('denoise'),
      shader('photon-clear'),
      shader('photon-trace'),
      shader('mlt-bootstrap'),
      shader('mlt-reduce'),
      shader('mlt-mutate'),
    ])
    const aoKernels = await Promise.all(
      ['ao-prepare', 'ao-pyramid', 'ao-main', 'ao-filter'].map(shader),
    )
    async function module(code) {
      const m = device.createShaderModule({ code })
      const info = await m.getCompilationInfo()
      const errors = info.messages.filter((m) => m.type === 'error')
      if (errors.length) throw Error(errors.map((m) => m.message).join('\n'))
      return m
    }
    const computeLayout = device.createBindGroupLayout({
      entries: Array.from({ length: 8 }, (_, binding) => ({
        binding,
        visibility: GPUShaderStage.COMPUTE,
        buffer: { type: binding >= 5 ? 'storage' : 'read-only-storage' },
      })),
    })
    const compute = await device.createComputePipelineAsync({
      layout: device.createPipelineLayout({ bindGroupLayouts: [computeLayout] }),
      compute: { module: await module(kernel.compute), entryPoint: 'CSMain' },
    })
    const aoPipelines = []
    for (let i = 0; i < 4; i++)
      aoPipelines.push(
        await device.createComputePipelineAsync({
          layout: device.createPipelineLayout({ bindGroupLayouts: [computeLayout] }),
          compute: {
            module: await module(aoKernels[i].compute),
            entryPoint: ['AOPrepare', 'AOPyramid', 'AOMain', 'AOFilter'][i],
          },
        }),
      )
    const denoise = await device.createComputePipelineAsync({
      layout: device.createPipelineLayout({ bindGroupLayouts: [computeLayout] }),
      compute: { module: await module(filter.compute), entryPoint: 'DenoiseMain' },
    })
    const photonPipelines = []
    for (const [shader, entryPoint] of [
      [clearPhotons, 'PhotonClear'],
      [tracePhotons, 'PhotonMain'],
    ])
      photonPipelines.push(
        await device.createComputePipelineAsync({
          layout: device.createPipelineLayout({ bindGroupLayouts: [computeLayout] }),
          compute: { module: await module(shader.compute), entryPoint },
        }),
      )
    const mltPipelines = []
    for (const [shader, entryPoint] of [
      [bootstrapMLT, 'MLTBootstrap'],
      [reduceMLT, 'MLTReduce'],
      [mutateMLT, 'MLTMutate'],
    ])
      mltPipelines.push(
        await device.createComputePipelineAsync({
          layout: device.createPipelineLayout({ bindGroupLayouts: [computeLayout] }),
          compute: { module: await module(shader.compute), entryPoint },
        }),
      )
    const presentLayout = device.createBindGroupLayout({
      entries: [0, 1].map((binding) => ({
        binding,
        visibility: GPUShaderStage.FRAGMENT,
        buffer: { type: 'read-only-storage' },
      })),
    })
    const present = await device.createRenderPipelineAsync({
      layout: device.createPipelineLayout({ bindGroupLayouts: [presentLayout] }),
      vertex: { module: await module(presentation.vert), entryPoint: 'VSMain' },
      fragment: {
        module: await module(presentation.frag),
        entryPoint: 'PSMain',
        targets: [{ format: navigator.gpu.getPreferredCanvasFormat() }],
      },
      primitive: { topology: 'triangle-list' },
    })
    const group = (layout, buffers) =>
      device.createBindGroup({
        layout,
        entries: buffers.map((b, binding) => ({ binding, resource: { buffer: b } })),
      })
    function updateEnvironment(environment) {
      if (skyBuffer && skySource === environment) return
      const sky = (environment?.backgroundMips || environment?.mips || fallbackEnvironment.mips)[0]
      // Temporarily restore relative offsets for the CPU builder.
      for (const i of [1, 2, 3]) lights.words[i] -= total / 4
      lights.updateEnvironment(sky, !!environment)
      for (const i of [1, 2, 3]) lights.words[i] += total / 4
      device.queue.writeBuffer(material, total, lights.words)
      const next = buffer(sky.pixels)
      release(skyBuffer)
      skyBuffer = next
      skySource = environment
      computeGroup = undefined
      samples = 0
    }
    return {
      get samples() {
        return samples
      },
      reset() {
        samples = 0
      },
      draw(
        context,
        camera,
        clear,
        fullWidth,
        fullHeight,
        lighting,
        light,
        environment,
        settings,
        path,
        ao,
        collection = null,
        shadows = { enabled: false, bias: 0.002 },
      ) {
        if (disposed) throw Error('Path view disposed')
        validatePathTracing(path)
        if (ao) ao = copyAmbientOcclusion(ao)
        const useAO = !!ao?.enabled,
          scale = useAO ? ao.resolutionScale : path.resolutionScale
        const w = Math.max(1, Math.round(fullWidth * scale)),
          h = Math.max(1, Math.round(fullHeight * scale))
        if (w * h > 16000000 || w * h * 16 > device.limits.maxStorageBufferBindingSize)
          throw Error('Path resolution exceeds WebGPU buffer limit')
        const photonCount = path.photonsPerIteration ?? 8192
        const requiredScratch = useAO
          ? w * h * 32
          : path.integrator === 'mlt'
            ? (14 * w * h +
                16 +
                (path.mltBootstrapSamples ?? 262144) * 2 +
                Math.min(1024, w * h) * 264) *
              4
            : w * h * 16 +
              (path.integrator === 'photon-mapping'
                ? (32768 + photonCount * path.maxBounces * 16) * 4
                : 0)
        if (requiredScratch > device.limits.maxStorageBufferBindingSize)
          throw Error(
            'Photon map exceeds storage buffer limit; reduce photon count or render scale',
          )
        if (w !== width || h !== height || requiredScratch !== scratchSize) {
          scratchSize = requiredScratch
          const nextAcc = buffer(new Uint8Array(w * h * 16)),
            nextOutput = buffer(
              new Uint8Array(w * h * 4),
              GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC,
            )
          const nextGuide = buffer(
            new Uint8Array(requiredScratch),
            GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC,
          )
          release(guides)
          guides = nextGuide
          release(accumulation)
          release(output)
          accumulation = nextAcc
          output = nextOutput
          width = w
          height = h
          samples = 0
          computeGroup = undefined
        }
        updateEnvironment(environment)
        const values = new Float32Array(388),
          words = new Uint32Array(values.buffer)
        values.set(
          sceneUniforms(
            data,
            camera,
            fullWidth / fullHeight,
            lighting,
            light,
            environment,
            settings,
          ),
        )
        if (collection !== null) {
          const records = lightRecords(collection)
          words[128] = 1
          words[129] = records.length / 48
          words[130] = shadows.enabled ? 1 : 0
          values[131] = shadows.bias
          for (let i = 0; i < records.length / 48; i++)
            values.set(records.subarray(i * 48 + 32, i * 48 + 48), 132 + i * 16)
        }
        words.set([0, w, h, path.maxBounces], 96)
        words.set(
          [
            path.samplesPerFrame,
            (environment?.backgroundMips || environment?.mips || fallbackEnvironment.mips)[0].size,
            bvh.count,
            (path.denoise ? 1 : 0) |
              (path.lightSampling !== false ||
              (path.integrator !== 'path' && path.integrator !== undefined)
                ? 2
                : 0) |
              (linearOutput ? 4 : 0) |
              (path.integrator === 'bidirectional' ? 8 : 0) |
              (path.integrator === 'photon-mapping' ? 16 : 0) |
              (path.integrator === 'mlt' ? 32 : 0),
          ],
          100,
        )
        values.set(clear, 104)
        words.set(metadata, 108)
        values[115] = path.photonRadius ?? 0.16
        words[119] = photonCount
        if (path.integrator === 'mlt') {
          words[107] = path.mltBootstrapSamples ?? 262144
          words[58] = path.mltSeed ?? 1
          values[123] = path.mltLargeStepProbability ?? 0.3
          values[127] = path.mltMutationScale ?? 0.01
        }
        if (useAO) {
          words[100] = 1
          words[103] = 64 | (ao.denoise ? 1 : 0) | (linearOutput ? 4 : 0)
          values[104] = ao.radius
          values[105] = ao.bias
          values[106] = ao.strength
          words[107] = ['disabled', 'ssao', 'hbao', 'msao', 'rtao'].indexOf(ao.method)
          words[115] = ['composite', 'occlusion', 'depth', 'normals'].indexOf(ao.output)
          words[119] = ao.samples
          words[123] = ao.seed
        }
        const signature = Array.from(words).join(',')
        if (signature !== previous || samples > 1000000) {
          samples = 0
          previous = signature
        }
        words[96] = samples
        device.queue.writeBuffer(params, 0, values)
        if (!computeGroup) {
          computeGroup = group(computeLayout, [
            nodes,
            params,
            vertices,
            material,
            skyBuffer,
            accumulation,
            output,
            guides,
          ])
          presentGroup = group(presentLayout, [output, params])
        }
        const encoder = device.createCommandEncoder()
        if (useAO && !samples) {
          for (let stage = 0; stage < (ao.method === 'msao' ? 2 : 1); stage++) {
            const prep = encoder.beginComputePass()
            prep.setPipeline(aoPipelines[stage])
            prep.setBindGroup(0, computeGroup)
            prep.dispatchWorkgroups(Math.ceil(w / 8), Math.ceil(h / 8))
            prep.end()
          }
        }
        if (!useAO && path.integrator === 'mlt') {
          for (let stage = samples ? 2 : 0; stage < 3; stage++) {
            const mltPass = encoder.beginComputePass()
            mltPass.setPipeline(mltPipelines[stage])
            mltPass.setBindGroup(0, computeGroup)
            const count =
              stage === 0
                ? Math.max(w * h, path.mltBootstrapSamples ?? 262144)
                : stage === 1
                  ? 1
                  : Math.min(1024, w * h)
            mltPass.dispatchWorkgroups(
              stage === 0
                ? Math.min(Math.ceil(count / 64), 1024)
                : stage === 1
                  ? 1
                  : Math.ceil(count / 64),
              stage === 0 ? Math.ceil(count / 65536) : 1,
            )
            mltPass.end()
          }
        }
        if (!useAO && path.integrator === 'photon-mapping') {
          for (let i = 0; i < 2; i++) {
            const photonPass = encoder.beginComputePass()
            photonPass.setPipeline(photonPipelines[i])
            photonPass.setBindGroup(0, computeGroup)
            photonPass.dispatchWorkgroups(i === 0 ? 512 : Math.ceil(photonCount / 64))
            photonPass.end()
          }
        }
        const pass = encoder.beginComputePass()
        pass.setPipeline(useAO ? aoPipelines[2] : compute)
        pass.setBindGroup(0, computeGroup)
        pass.dispatchWorkgroups(Math.ceil(w / 8), Math.ceil(h / 8))
        pass.end()
        if (useAO ? ao.denoise : path.denoise) {
          const filterPass = encoder.beginComputePass()
          filterPass.setPipeline(useAO ? aoPipelines[3] : denoise)
          filterPass.setBindGroup(0, computeGroup)
          filterPass.dispatchWorkgroups(Math.ceil(w / 8), Math.ceil(h / 8))
          filterPass.end()
        }
        if (context) {
          const display = encoder.beginRenderPass({
            colorAttachments: [
              {
                view: context.getCurrentTexture().createView(),
                loadOp: 'clear',
                storeOp: 'store',
                clearValue: clear,
              },
            ],
          })
          display.setPipeline(present)
          display.setBindGroup(0, presentGroup)
          display.draw(3)
          display.end()
        }
        device.queue.submit([encoder.finish()])
        samples += useAO ? 1 : path.samplesPerFrame
      },
      present(context, rectangle, clear = true) {
        if (!presentGroup) return
        const encoder = device.createCommandEncoder(),
          pass = encoder.beginRenderPass({
            colorAttachments: [
              {
                view: context.getCurrentTexture().createView(),
                loadOp: clear ? 'clear' : 'load',
                storeOp: 'store',
                clearValue: [0, 0, 0, 1],
              },
            ],
          })
        pass.setPipeline(present)
        pass.setBindGroup(0, presentGroup)
        pass.setViewport(rectangle.x, rectangle.y, rectangle.width, rectangle.height, 0, 1)
        pass.draw(3)
        pass.end()
        device.queue.submit([encoder.finish()])
      },
      async readMLTStatistics() {
        const read = device.createBuffer({
          size: 32,
          usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
        })
        try {
          const encoder = device.createCommandEncoder()
          encoder.copyBufferToBuffer(guides, width * height * 14 * 4, read, 0, 32)
          device.queue.submit([encoder.finish()])
          await read.mapAsync(GPUMapMode.READ)
          const mapped = read.getMappedRange(),
            u = new Uint32Array(mapped),
            f = new Float32Array(mapped)
          return {
            normalization: f[0],
            accepted: u[1],
            proposals: u[2],
            dimensionOverflow: u[3],
            normalizationSE: f[4],
            positiveBootstrap: u[5],
          }
        } finally {
          read.destroy()
        }
      },
      async readPixels() {
        const read = device.createBuffer({
          size: width * height * 4,
          usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
        })
        try {
          const encoder = device.createCommandEncoder()
          encoder.copyBufferToBuffer(output, 0, read, 0, width * height * 4)
          device.queue.submit([encoder.finish()])
          await read.mapAsync(GPUMapMode.READ)
          return { width, height, pixels: new Uint8Array(read.getMappedRange()).slice() }
        } finally {
          read.destroy()
        }
      },
      dispose() {
        if (disposed) return
        disposed = true
        for (const r of resources) r.destroy()
      },
    }
  } catch (error) {
    for (const r of resources) r.destroy()
    throw error
  }
}
