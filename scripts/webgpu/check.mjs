import { chromium } from 'playwright'
import { readFile } from 'node:fs/promises'
const data = JSON.parse(await readFile('docs/public/generated/triangle.json', 'utf8'))
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage()
  await page.goto('http://127.0.0.1:5173')
  console.log(
    await page.evaluate(async (d) => {
      const adapter = await navigator.gpu?.requestAdapter()
      if (!adapter) throw Error('No WebGPU adapter')
      const device = await adapter.requestDevice()
      device.pushErrorScope('validation')
      const pipeline = await device.createRenderPipelineAsync({
        layout: 'auto',
        vertex: { module: device.createShaderModule({ code: d.vert }), entryPoint: 'VSMain' },
        fragment: {
          module: device.createShaderModule({ code: d.frag }),
          entryPoint: 'PSMain',
          targets: [{ format: 'rgba8unorm' }],
        },
        primitive: { topology: 'triangle-list' },
      })
      const texture = device.createTexture({
        size: [256, 256],
        format: 'rgba8unorm',
        usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
      })
      const buffer = device.createBuffer({
        size: 256 * 256 * 4,
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
      })
      const enc = device.createCommandEncoder()
      const pass = enc.beginRenderPass({
        colorAttachments: [
          {
            view: texture.createView(),
            clearValue: { r: 0, g: 0, b: 0, a: 1 },
            loadOp: 'clear',
            storeOp: 'store',
          },
        ],
      })
      pass.setPipeline(pipeline)
      pass.draw(3)
      pass.end()
      enc.copyTextureToBuffer({ texture }, { buffer, bytesPerRow: 1024 }, [256, 256])
      device.queue.submit([enc.finish()])
      await buffer.mapAsync(GPUMapMode.READ)
      const bytes = new Uint8Array(buffer.getMappedRange())
      const pixel = (x, y) => Array.from(bytes.slice((y * 256 + x) * 4, (y * 256 + x) * 4 + 4))
      const samples = {
        top: pixel(128, 65),
        left: pixel(65, 180),
        right: pixel(190, 180),
        background: pixel(5, 5),
      }
      const error = await device.popErrorScope()
      if (error) throw Error(error.message)
      if (
        !(
          samples.top[0] > samples.top[1] &&
          samples.left[2] > samples.left[0] &&
          samples.right[1] > samples.right[0]
        )
      )
        throw Error(JSON.stringify(samples))
      return {
        adapter: {
          vendor: adapter.info.vendor,
          architecture: adapter.info.architecture,
          device: adapter.info.device,
          description: adapter.info.description,
          isFallbackAdapter: adapter.info.isFallbackAdapter,
        },
        samples,
      }
    }, data),
  )
} finally {
  await browser.close()
}
