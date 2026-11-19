import { chromium } from 'playwright'
import { readFile, writeFile, mkdir } from 'node:fs/promises'
import { resolve } from 'node:path'
import { compile } from './compiler.mjs'
const source = await readFile('render/shaders/pathtrace.hlsl', 'utf8')
const probe = await compile(
  source.replace('void CSMain(', 'void PathMain(') +
    `
[numthreads(1,1,1)] void CSMain(uint3 id : SV_DispatchThreadID) {
 Hit hit; bool found=trace(parameters[id.x*2].xyz,parameters[id.x*2+1].xyz,hit);
 uint offset=id.x*5; outputImage[offset]=found?1:0;outputImage[offset+1]=asuint(hit.distance);
 outputImage[offset+2]=hit.primitive;outputImage[offset+3]=asuint(hit.bary.x);outputImage[offset+4]=asuint(hit.bary.y);
}`,
  { compute: true },
)
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage()
  await page.goto(process.env.NOVA_TEST_URL || 'http://127.0.0.1:5173/')
  const result = await page.evaluate(
    async ({ probe, moduleBase }) => {
      const { buildBVH } = await import(moduleBase + '/BVH.js'),
        { createPathView } = await import(moduleBase + '/PathView.js'),
        { loadGLB, emptyModelData } = await import(moduleBase + '/ModelData.js'),
        { loadEnvironmentFile, defaultEnvironment } = await import(
          moduleBase + '/EnvironmentData.js'
        )
      const adapter = await navigator.gpu.requestAdapter(),
        device = await adapter.requestDevice()
      const errors = []
      device.addEventListener('uncapturederror', (e) => errors.push(e.error.message))
      device.pushErrorScope('validation')
      let path
      try {
        const data = emptyModelData()
        data.vertices = new Float32Array([
          -1, -1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, -1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0,
          0, 1, 0, 0.5, 1, 0, 0,
        ])
        data.baseColor = [1, 0, 0, 1]
        const bvh = buildBVH(data.vertices)
        const params = new Float32Array(388)
        const words = new Uint32Array(params.buffer)
        words[102] = bvh.count
        const rays = [
          [
            [0, 0, 1],
            [0, 0, -1],
          ],
          [
            [0, 0, -1],
            [0, 0, 1],
          ],
          [
            [2, 0, 1],
            [0, 0, -1],
          ],
          [
            [0, 0, 0.0001],
            [0, 0, -1],
          ],
          [
            [0, 0, 1],
            [1, 0, 0],
          ],
          [
            [0, -1, 1],
            [0, 0, -1],
          ],
        ]
        rays.forEach((r, i) => {
          params.set(r[0], i * 8)
          params.set(r[1], i * 8 + 4)
        })
        function buf(bytes, usage = GPUBufferUsage.STORAGE) {
          const b = device.createBuffer({
            size: Math.max(16, bytes.byteLength),
            usage: usage | GPUBufferUsage.COPY_DST,
          })
          device.queue.writeBuffer(b, 0, bytes)
          return b
        }
        const buffers = [
          buf(bvh.nodes),
          buf(params),
          buf(bvh.vertices),
          buf(new Uint32Array(4)),
          buf(new Uint32Array(4)),
          buf(new Float32Array(4)),
          buf(new Uint32Array(rays.length * 5), GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC),
        ]
        const layout = device.createBindGroupLayout({
          entries: buffers.map((_, binding) => ({
            binding,
            visibility: GPUShaderStage.COMPUTE,
            buffer: { type: binding >= 5 ? 'storage' : 'read-only-storage' },
          })),
        })
        const pipeline = await device.createComputePipelineAsync({
          layout: device.createPipelineLayout({ bindGroupLayouts: [layout] }),
          compute: {
            module: device.createShaderModule({ code: probe.compute }),
            entryPoint: 'CSMain',
          },
        })
        const group = device.createBindGroup({
          layout,
          entries: buffers.map((buffer, binding) => ({ binding, resource: { buffer } })),
        })
        const read = device.createBuffer({
            size: rays.length * 20,
            usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
          }),
          encoder = device.createCommandEncoder(),
          pass = encoder.beginComputePass()
        pass.setPipeline(pipeline)
        pass.setBindGroup(0, group)
        pass.dispatchWorkgroups(rays.length)
        pass.end()
        encoder.copyBufferToBuffer(buffers[6], 0, read, 0, rays.length * 20)
        device.queue.submit([encoder.finish()])
        await read.mapAsync(GPUMapMode.READ)
        const hits = new Uint32Array(read.getMappedRange()).slice(),
          floats = new Float32Array(hits.buffer)
        for (let i = 0; i < rays.length; i++)
          if (hits[i * 5] !== ([0, 1, 5].includes(i) ? 1 : 0))
            throw Error('BVH ray hit mismatch ' + i)
        if (
          Math.abs(floats[1] - 1) > 1e-5 ||
          Math.abs(floats[3] - 0.25) > 1e-5 ||
          Math.abs(floats[4] - 0.5) > 1e-5
        )
          throw Error('BVH distance/barycentrics mismatch')
        read.destroy()
        buffers.forEach((b) => b.destroy())
        const camera = { yaw: 20, pitch: 5, distance: 3.6, exposure: 1 },
          light = { direction: [-1, 1.6, 2], color: [1, 1, 1], intensity: 3 },
          settings = defaultEnvironment(),
          options = { samplesPerFrame: 2, maxBounces: 3, resolutionScale: 1 }
        path = await createPathView(device, data, '/generated/')
        path.draw(
          null,
          camera,
          [0.02, 0.03, 0.05, 1],
          64,
          64,
          'pbr',
          light,
          null,
          settings,
          options,
        )
        let image = await path.readPixels()
        const center = (32 * 64 + 32) * 4
        if (image.pixels[center] < 50 || image.pixels[center] <= image.pixels[center + 1] * 2)
          throw Error('Triangle pixels not red')
        path.dispose()
        const helmet = await loadGLB('/generated/DamagedHelmet.glb'),
          env = await loadEnvironmentFile('/generated/venice_sunset.bin')
        try {
          path = await createPathView(device, helmet, '/generated/')
        } finally {
          helmet.images.forEach((i) => i.bitmap?.close())
        }
        light.intensity = 0
        const shots = []
        for (let i = 0; i < 17; i++) {
          path.draw(
            null,
            camera,
            [0.02, 0.03, 0.05, 1],
            160,
            120,
            'pbr',
            light,
            env,
            settings,
            options,
          )
          if ([0, 3, 16].includes(i)) shots.push(await path.readPixels())
        }
        const mse = (a, b) => {
          let sum = 0,
            count = 0
          for (let y = 20; y < 100; y++)
            for (let x = 30; x < 130; x++)
              for (let c = 0; c < 3; c++) {
                const k = (y * 160 + x) * 4 + c
                sum += (a[k] - b[k]) ** 2
                count++
              }
          return sum / count
        }
        const early = mse(shots[0].pixels, shots[2].pixels),
          later = mse(shots[1].pixels, shots[2].pixels)
        if (!(later < early * 0.8))
          throw Error('No progressive convergence: ' + early + ' -> ' + later)
        path.draw(null, camera, [0.02, 0.03, 0.05, 1], 160, 120, 'pbr', light, env, settings, {
          ...options,
          denoise: true,
        })
        const filtered = await path.readPixels(),
          filteredError = mse(filtered.pixels, shots[2].pixels)
        if (!(filteredError < early) || path.samples !== 2)
          throw Error(
            'Denoise did not improve reference error/reset samples: ' +
              filteredError +
              ' vs ' +
              early,
          )
        for (let c = 0; c < 4; c++)
          if (filtered.pixels[c] !== shots[0].pixels[c]) throw Error('Sky changed by denoise')
        path.draw(
          null,
          camera,
          [0.02, 0.03, 0.05, 1],
          160,
          120,
          'pbr',
          light,
          env,
          settings,
          options,
        )
        const restored = await path.readPixels()
        if (restored.pixels.some((v, i) => v !== shots[0].pixels[i]))
          throw Error('Raw output not restored')
        console.log('Denoise MSE', early, filteredError)
        if (path.samples !== 2) throw Error('Sample count')
        path.draw(
          null,
          { ...camera, yaw: 21 },
          [0.02, 0.03, 0.05, 1],
          160,
          120,
          'pbr',
          light,
          env,
          settings,
          options,
        )
        if (path.samples !== 2) throw Error('Camera did not reset accumulation')
        path.reset()
        if (path.samples !== 0) throw Error('Manual reset')
        const error = await device.popErrorScope()
        if (error || errors.length) throw Error(error?.message || errors.join('\n'))
        const canvas = document.createElement('canvas')
        canvas.width = 160
        canvas.height = 120
        canvas
          .getContext('2d')
          .putImageData(new ImageData(new Uint8ClampedArray(shots[2].pixels), 160, 120), 0, 0)
        return {
          early,
          later,
          filteredError,
          triangles: helmet.vertices.length / 48,
          nodes: buildBVH(helmet.vertices).count,
          png: canvas.toDataURL().split(',')[1],
        }
      } finally {
        path?.dispose()
        device.destroy()
      }
    },
    { probe, moduleBase: '/@fs' + resolve('web/src') },
  )
  await mkdir('build/validation', { recursive: true })
  await writeFile('build/validation/web-path-helmet.png', Buffer.from(result.png, 'base64'))
  delete result.png
  console.log(
    'PASS HLSL→WGSL BVH front/back/miss/parallel/near/edge rays, red pixels, Helmet HDR convergence and reset',
    result,
  )
} finally {
  await browser.close()
}
