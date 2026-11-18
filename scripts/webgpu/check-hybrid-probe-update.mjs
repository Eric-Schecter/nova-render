import { readFile } from 'node:fs/promises'
import { chromium } from 'playwright'
import { compile } from './compiler.mjs'

const source = await readFile('render/shaders/model.hlsl', 'utf8')
const { compute } = await compile(source, {
  compute: true,
  computeEntry: 'HybridProbeUpdateCS',
  optimizeCompute: true,
  maxSourceBytes: 128 * 1024,
})
const { compute: sampling } = await compile('#define NOVA_PROBE_SAMPLE_VALIDATION 1\n' + source, {
  compute: true,
  computeEntry: 'HybridProbeSampleCS',
  optimizeCompute: true,
  maxSourceBytes: 128 * 1024,
})
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage()
  await page.goto(process.env.NOVA_TEST_URL || 'http://127.0.0.1:5220')
  await page.evaluate(async (code) => {
    const adapter = await navigator.gpu.requestAdapter()
    const device = await adapter.requestDevice()
    const pipeline = await device.createComputePipelineAsync({
      layout: 'auto',
      compute: { module: device.createShaderModule({ code }), entryPoint: 'HybridProbeUpdateCS' },
    })
    const buffer = (data, usage) => {
      const b = device.createBuffer({ size: data.byteLength, usage, mappedAtCreation: true })
      new Uint8Array(b.getMappedRange()).set(new Uint8Array(data.buffer))
      b.unmap()
      return b
    }
    const bind = (group, entries) =>
      device.createBindGroup({ layout: pipeline.getBindGroupLayout(group), entries })
    const materials = device.createTexture({
      size: [1, 1],
      format: 'rgba8unorm',
      usage: GPUTextureUsage.TEXTURE_BINDING,
    })
    const environment = device.createTexture({
      size: [1, 1, 6],
      format: 'rgba8unorm',
      usage: GPUTextureUsage.TEXTURE_BINDING,
    })
    // An empty scene returns zero incoming light and the maximum distance for every ray.
    const scene = bind(0, [
      { binding: 0, resource: { buffer: buffer(new Float32Array(256), GPUBufferUsage.UNIFORM) } },
      ...[2, 3, 4, 5, 6].map((binding) => ({ binding, resource: materials.createView() })),
      { binding: 7, resource: device.createSampler() },
      { binding: 8, resource: environment.createView({ dimension: 'cube' }) },
      { binding: 14, resource: { buffer: buffer(new Float32Array(48), GPUBufferUsage.STORAGE) } },
    ])
    const settings = new Float32Array(16)
    settings.set([1, 1, 1, 10], 4)
    const words = new Uint32Array(settings.buffer)
    words.set([2, 1, 1, 1, 1, 7], 8)
    settings[14] = 0.5
    const previous = new Float32Array(2 * 6 * 8)
    previous.set([2, 4, 6, 1, 0.5, 0.25, 0, 0], 6 * 8)
    const updated = buffer(
      new Float32Array(previous.length),
      GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC,
    )
    const batch = bind(2, [
      { binding: 25, resource: { buffer: buffer(settings, GPUBufferUsage.UNIFORM) } },
      { binding: 26, resource: { buffer: buffer(previous, GPUBufferUsage.STORAGE) } },
      { binding: 27, resource: { buffer: updated } },
    ])
    const readback = device.createBuffer({
      size: previous.byteLength,
      usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
    })
    const encoder = device.createCommandEncoder()
    const pass = encoder.beginComputePass()
    pass.setPipeline(pipeline)
    pass.setBindGroup(0, scene)
    pass.setBindGroup(2, batch)
    pass.dispatchWorkgroups(1)
    pass.end()
    encoder.copyBufferToBuffer(updated, 0, readback, 0, previous.byteLength)
    device.queue.submit([encoder.finish()])
    await readback.mapAsync(GPUMapMode.READ)
    const values = new Float32Array(readback.getMappedRange())
    const check = (actual, expected) =>
      expected.forEach((value, i) => {
        if (Math.abs(actual[i] - value) > 1e-6 || !Number.isFinite(actual[i]))
          throw Error('Unexpected probe value at ' + i + ': ' + actual[i])
      })
    check(values.subarray(0, 48), new Float32Array(48))
    check(values.subarray(48, 56), [1, 2, 3, 1, 0.75, 0.625, 0, 0])
    for (let i = 56; i < values.length; i += 8)
      check(values.subarray(i, i + 8), [0, 0, 0, 1, 1, 1, 0, 0])
    readback.unmap()
    device.destroy()
  }, compute)
  await page.evaluate(async (code) => {
    const adapter = await navigator.gpu.requestAdapter()
    const device = await adapter.requestDevice()
    const pipeline = await device.createComputePipelineAsync({
      layout: 'auto',
      compute: { module: device.createShaderModule({ code }), entryPoint: 'HybridProbeSampleCS' },
    })
    const make = (data, usage) => {
      const buffer = device.createBuffer({ size: data.byteLength, usage, mappedAtCreation: true })
      new Float32Array(buffer.getMappedRange()).set(data)
      buffer.unmap()
      return buffer
    }
    const positions = new Float32Array([
      0.01, 0, 0, 0, -1, 0, 0, 0, 0.45, 0, 0, 0, -1, 0, 0, 0, 0.6, 0, 0, 0, -1, 0, 0, 0, 0.01, 0, 0,
      0, 0, 1, 0, 0,
    ])
    const data = new Float32Array(6 * 8)
    for (let i = 0; i < 6; i++) data.set([i + 1, 0, 0, 1, 0.02, 0.0004, 0, 0], i * 8)
    const settings = new Float32Array(16)
    settings.set([1, 1, 1, 1], 4)
    // Write the integer fields directly into the mapped buffer.
    const uniform = device.createBuffer({
      size: settings.byteLength,
      usage: GPUBufferUsage.UNIFORM,
      mappedAtCreation: true,
    })
    const memory = uniform.getMappedRange()
    new Float32Array(memory).set(settings)
    new Uint32Array(memory).set([1, 1, 1, 0, 4], 8)
    uniform.unmap()
    const output = make(new Float32Array(16), GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC)
    const group = device.createBindGroup({
      layout: pipeline.getBindGroupLayout(2),
      entries: [
        [25, uniform],
        [26, make(data, GPUBufferUsage.STORAGE)],
        [3, make(positions, GPUBufferUsage.STORAGE)],
        [4, output],
      ].map(([binding, buffer]) => ({ binding, resource: { buffer } })),
    })
    const readback = device.createBuffer({
      size: 64,
      usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
    })
    const encoder = device.createCommandEncoder()
    const pass = encoder.beginComputePass()
    pass.setPipeline(pipeline)
    pass.setBindGroup(2, group)
    pass.dispatchWorkgroups(1)
    pass.end()
    encoder.copyBufferToBuffer(output, 0, readback, 0, 64)
    device.queue.submit([encoder.finish()])
    await readback.mapAsync(GPUMapMode.READ)
    const rgb = new Float32Array(readback.getMappedRange())
    if (rgb[0] !== 2 || rgb[4] >= 0.0001 || rgb[8] !== 0 || rgb[12] !== 1.5)
      throw Error('Probe directional weighting or occlusion failed: ' + Array.from(rgb))
    readback.unmap()
    device.destroy()
  }, sampling)
  console.log(
    'Probe update batch bounds, hysteresis, directional sampling and occlusion passed on WebGPU',
  )
} finally {
  await browser.close()
}
