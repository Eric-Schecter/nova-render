import { buildBVH } from './BVH.js'
import { sceneUniforms } from './ModelData.js'
import { fallbackEnvironment } from './EnvironmentData.js'
export const defaultPathTracing = () => ({
  samplesPerFrame: 1,
  maxBounces: 3,
  resolutionScale: 0.5,
})
export function validatePathTracing(value) {
  if (
    !value ||
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
export async function createPathView(device, data, shaderDirectory) {
  const resources = new Set()
  let accumulation,
    output,
    computeGroup,
    presentGroup,
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
      params = buffer(new Uint8Array(512))
    const metadata = [],
      images = []
    let total = 0
    for (const image of data.images) {
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
    const texels = new Uint8Array(total)
    let offset = 0
    for (const bytes of images) {
      texels.set(bytes, offset)
      offset += bytes.byteLength
    }
    const material = buffer(texels)
    async function shader(name) {
      const response = await fetch(shaderDirectory.replace(/\/?$/, '/') + name + '.json')
      if (!response.ok) throw Error('Missing path shader package: ' + name)
      return response.json()
    }
    const [kernel, presentation] = await Promise.all([shader('pathtrace'), shader('pathpresent')])
    async function module(code) {
      const m = device.createShaderModule({ code })
      const info = await m.getCompilationInfo()
      const errors = info.messages.filter((m) => m.type === 'error')
      if (errors.length) throw Error(errors.map((m) => m.message).join('\n'))
      return m
    }
    const computeLayout = device.createBindGroupLayout({
      entries: Array.from({ length: 7 }, (_, binding) => ({
        binding,
        visibility: GPUShaderStage.COMPUTE,
        buffer: { type: binding >= 5 ? 'storage' : 'read-only-storage' },
      })),
    })
    const compute = await device.createComputePipelineAsync({
      layout: device.createPipelineLayout({ bindGroupLayouts: [computeLayout] }),
      compute: { module: await module(kernel.compute), entryPoint: 'CSMain' },
    })
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
      ) {
        if (disposed) throw Error('Path view disposed')
        validatePathTracing(path)
        const w = Math.max(1, Math.round(fullWidth * path.resolutionScale)),
          h = Math.max(1, Math.round(fullHeight * path.resolutionScale))
        if (w * h > 16000000 || w * h * 16 > device.limits.maxStorageBufferBindingSize)
          throw Error('Path resolution exceeds WebGPU buffer limit')
        if (w !== width || h !== height) {
          const nextAcc = buffer(new Uint8Array(w * h * 16)),
            nextOutput = buffer(
              new Uint8Array(w * h * 4),
              GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC,
            )
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
        const values = new Float32Array(128),
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
        words.set([0, w, h, path.maxBounces], 96)
        words.set(
          [
            path.samplesPerFrame,
            (environment?.backgroundMips || environment?.mips || fallbackEnvironment.mips)[0].size,
            bvh.count,
            0,
          ],
          100,
        )
        values.set(clear, 104)
        words.set(metadata, 108)
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
          ])
          presentGroup = group(presentLayout, [output, params])
        }
        const encoder = device.createCommandEncoder(),
          pass = encoder.beginComputePass()
        pass.setPipeline(compute)
        pass.setBindGroup(0, computeGroup)
        pass.dispatchWorkgroups(Math.ceil(w / 8), Math.ceil(h / 8))
        pass.end()
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
        samples += path.samplesPerFrame
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
