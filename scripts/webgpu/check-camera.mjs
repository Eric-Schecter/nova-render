import { chromium } from 'playwright'
import { realpathSync, readFileSync, mkdirSync, writeFileSync } from 'node:fs'
import { decode } from './Screenshot.mjs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5214'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
mkdirSync('build/validation/camera', { recursive: true })
try {
  const page = await browser.newPage({
      viewport: { width: 900, height: 760 },
      deviceScaleFactor: 2,
    }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(base)
  // Copy exactly the buffer range, avoiding Node's pooled-buffer padding.
  const bytes = readFileSync('build/camera-uniforms.bin'),
    uniforms = Array.from(new Float32Array(bytes.buffer, bytes.byteOffset, bytes.byteLength / 4))
  await page.evaluate(
    async ({ root, uniforms }) => {
      const { sceneUniforms, emptyModelData } = await import(root + '/web/src/ModelData.js')
      const { createRenderer } = await import(root + '/web/src/Nova.js')
      const require = (v, m) => {
        if (!v) throw Error(m)
      }
      for (let i = 0; i < 4; i++) {
        const camera = {
          position: [1, 2, 5],
          orientation: i >= 2 ? [0.2, 0.3, 0.1, 0.8] : [0, 0, 0, 1],
          projection: i % 2 ? 'orthographic' : 'perspective',
          verticalFov: 35 + i * 15,
          nearPlane: 0.1,
          farPlane: 80,
          orthographicHeight: 4,
          exposure: 1,
        }
        const u = sceneUniforms(emptyModelData(), camera, 1.5)
        for (const k of [
          ...Array(20).keys(),
          ...Array.from({ length: 12 }, (_, i) => i + 40),
          67,
          71,
        ])
          require(Math.abs(u[k] - uniforms[i * 96 + k]) <
            1e-5, 'Native/Web camera uniforms differ ' + i + ':' + k)
      }
      const canvas = document.createElement('canvas')
      canvas.style = 'width:400px;height:300px'
      document.body.replaceChildren(canvas)
      const r = await createRenderer({ canvas, shaderDirectory: '/generated/' })
      r.setContinuousRendering(false)
      r.setClearColor([0, 0, 0, 1])
      await r.loadModel('/generated/SceneRed.glb')
      r.setViewCamera({
        position: [0, 0, 5],
        orientation: [0, 0, 0, 1],
        projection: 'orthographic',
        orthographicHeight: 4,
      })
      r.setViewport({ x: 80, y: 60, width: 320, height: 240 })
      await r.drawFrame()
      require(canvas.width === 800 && canvas.height === 600, 'DPI target size incorrect')
      const copy = r.getViewCamera()
      copy.position[0] = 123
      require(r.getViewCamera().position[0] === 0, 'Camera getter aliases internal state')
      let rejected = 0
      for (const bad of [
        { orientation: [0, 0, 0, 0] },
        { nearPlane: 0 },
        { nearPlane: 10, farPlane: 1 },
        { verticalFov: 180 },
        { projection: 'invalid' },
      ])
        try {
          r.setViewCamera(bad)
        } catch {
          rejected++
        }
      require(rejected === 5, 'Invalid camera accepted')
      try {
        r.setViewport({ x: 0, y: 0, width: 0, height: 20 })
        throw Error('Invalid viewport accepted')
      } catch (e) {
        if (e.message === 'Invalid viewport accepted') throw e
      }
      window.cameraTest = { r, require }
    },
    { root: '/@fs' + realpathSync('.'), uniforms },
  )
  async function image(name, empty = false, blue = false) {
    const png = await page.locator('canvas').screenshot(),
      im = decode(png)
    let maxColor = 0
    let count = 0,
      minX = im.width,
      minY = im.height,
      maxX = -1,
      maxY = -1
    for (let y = 0; y < im.height; y++)
      for (let x = 0; x < im.width; x++) {
        const k = (y * im.width + x) * im.channels
        maxColor = Math.max(maxColor, im.pixels[k], im.pixels[k + 1], im.pixels[k + 2])
        if (
          blue
            ? im.pixels[k + 2] > 150 && im.pixels[k + 2] > im.pixels[k] * 1.2
            : im.pixels[k] > 150 && im.pixels[k] > im.pixels[k + 2] * 2
        ) {
          count++
          minX = Math.min(minX, x)
          minY = Math.min(minY, y)
          maxX = Math.max(maxX, x)
          maxY = Math.max(maxY, y)
        }
      }
    if (empty ? maxColor > 1 : count < 50) throw Error(name + ': unexpected image ' + count)
    if (!empty && (minX < 80 || minY < 60 || maxX >= 400 || maxY >= 300))
      throw Error(name + ': geometry escaped framebuffer-pixel viewport')
    writeFileSync(`build/validation/camera/${name}.png`, png)
    return { count, width: maxX - minX + 1, height: maxY - minY + 1 }
  }
  const reference = await image('orthographic')
  await page.evaluate(async () => {
    const { r } = window.cameraTest
    r.setViewCamera({ position: [0, 0, 10] })
    await r.drawFrame()
  })
  const distant = await image('orthographic-distant')
  if (JSON.stringify(reference) !== JSON.stringify(distant))
    throw Error('Orthographic scale depends on distance')
  await page.evaluate(async () => {
    const { r } = window.cameraTest
    r.setViewCamera({ position: [0, 0, 5], projection: 'perspective', verticalFov: 35 })
    await r.drawFrame()
  })
  const narrow = await image('perspective-35')
  await page.evaluate(async () => {
    const { r } = window.cameraTest
    r.setViewCamera({ verticalFov: 90 })
    await r.drawFrame()
  })
  const wide = await image('perspective-90')
  if (wide.width >= narrow.width) throw Error('FOV did not change framing')
  for (const method of ['off', 'fxaa', 'smaa', 'msaa', 'ssaa', 'taa']) {
    await page.evaluate(async (method) => {
      const { r } = window.cameraTest
      r.setViewCamera({
        projection: 'orthographic',
        position: [0, 0, 5],
        nearPlane: 0.01,
        farPlane: 200,
      })
      r.setAntiAliasing({ method })
      await r.drawFrame()
    }, method)
    await image('viewport-' + method)
  }
  await page.evaluate(async () => {
    const { r, require } = window.cameraTest
    r.setViewCamera(r.getViewCamera())
    r.setViewport(r.getViewport())
    await r.drawFrame()
    require(r.getAntiAliasingHistoryFrames() === 2, 'Unchanged camera reset history')
  })
  await page.evaluate(async () => {
    const { r } = window.cameraTest
    r.setViewCamera({ orientation: [0, 0, 0.38268343, 0.9238795] })
    await r.drawFrame()
  })
  const rolled = await image('roll')
  if (rolled.height <= reference.height) throw Error('Roll did not rotate framing')
  await page.evaluate(async () => {
    const { r } = window.cameraTest
    r.setViewCamera({ position: [0, 5, 0], orientation: [-0.70710678, 0, 0, 0.70710678] })
    await r.drawFrame()
  })
  await image('vertical-view')
  await page.evaluate(() =>
    window.cameraTest.r.setViewCamera({ position: [0, 0, 5], orientation: [0, 0, 0, 1] }),
  )
  await page.evaluate(async () => {
    const { r } = window.cameraTest
    r.setViewCamera({ nearPlane: 6, farPlane: 10 })
    await r.drawFrame()
  })
  await image('near-clipped', true)
  await page.evaluate(async () => {
    const { r } = window.cameraTest
    r.setViewCamera({ nearPlane: 0.01, farPlane: 4 })
    await r.drawFrame()
  })
  await image('far-clipped', true)
  await page.evaluate(async () => {
    const { r, require } = window.cameraTest
    r.setViewCamera({ nearPlane: 0.01, farPlane: 200, orientation: [0, 0, 0, 1] })
    r.setPostProcessing({ enabled: true, bloom: false })
    await r.drawFrame()
    r.setViewport({ x: 900, y: 900, width: 10, height: 10 })
    require((await r.drawFrame()) === 'skipped', 'Outside viewport did not suspend')
    r.setViewport(null)
    await r.drawFrame()
    require(r.getViewport() === null, 'Viewport reset failed')
    r.setCamera({ yaw: 0 })
    require(r.getViewCamera().position[2] === r.getCamera().distance, 'Legacy orbit did not resume')
  })
  await page.evaluate(async () => {
    const { r } = window.cameraTest
    r.setPostProcessing({ enabled: false })
    r.setAntiAliasing({ method: 'off' })
    r.setLighting('pbr')
    r.setEnvironment({ ...r.getEnvironment(), showBackground: false })
    r.setViewport({ x: 80, y: 60, width: 320, height: 240 })
    r.setViewCamera({
      position: [0, 0, 5],
      orientation: [0, 0, 0, 1],
      projection: 'orthographic',
      nearPlane: 0.01,
      farPlane: 200,
    })
    r.setPathTracing({ samplesPerFrame: 1, maxBounces: 1, resolutionScale: 1 })
    await r.setRenderMode('ray-tracing')
    await r.drawFrame()
  })
  await image('ray-orthographic')
  await page.evaluate(async () => {
    const { r } = window.cameraTest
    r.setRenderMode('raster')
    r.setAmbientOcclusion({
      enabled: true,
      method: 'disabled',
      output: 'normals',
      denoise: false,
      resolutionScale: 1,
    })
    r.setViewCamera({ nearPlane: 5.1, farPlane: 6 })
    await r.drawFrame()
  })
  await image('ray-clipped-front-visible-back', false, true)
  await page.evaluate(async () => {
    const { r } = window.cameraTest
    r.setAmbientOcclusion({ enabled: false })
    r.setRenderMode('ray-tracing')
    r.setViewCamera({ nearPlane: 6, farPlane: 10 })
    await r.drawFrame()
  })
  await image('ray-near-clipped', true)
  await page.evaluate(async () => {
    const { r } = window.cameraTest
    r.setViewCamera({ nearPlane: 0.01, farPlane: 4 })
    await r.drawFrame()
  })
  await image('ray-far-clipped', true)
  await page.evaluate(() => window.cameraTest.r.dispose())
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS camera parity, FOV, orthographic size, roll, clip planes, all AA viewport placement, DPI, resize and invalid input',
  )
} finally {
  await browser.close()
}
