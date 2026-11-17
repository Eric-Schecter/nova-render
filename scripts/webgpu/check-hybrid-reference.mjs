import { chromium } from 'playwright'
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { decode } from './Screenshot.mjs'
const fixture = JSON.parse(readFileSync('build/reference/hybrid-rendering/scenes.json')),
  dir = 'build/validation/hybrid-reference/web'
mkdirSync(dir, { recursive: true })
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
const difference = (a, b) => {
  if (a.width !== b.width || a.height !== b.height) throw Error('Image size')
  let n = 0
  for (let i = 0; i < a.width * a.height; i++)
    for (let c = 0; c < 3; c++)
      n += Math.abs(a.pixels[i * a.channels + c] - b.pixels[i * b.channels + c])
  return n / (a.width * a.height * 3)
}
try {
  const page = await browser.newPage(),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(process.env.NOVA_TEST_URL || 'http://127.0.0.1:5220')
  await page.evaluate(async (f) => {
    const { createRenderer } = await import(
      '/@fs/Users/eric/Documents/repos/renderer/nova-v2/web/src/Nova.js'
    )
    const canvas = document.createElement('canvas')
    canvas.width = f.width
    canvas.height = f.height
    document.body.replaceChildren(canvas)
    window.r = await createRenderer({ canvas })
    r.setContinuousRendering(false)
    r.setPostProcessing({ enabled: true, bloom: false, autoExposure: false, toneMapping: 'aces' })
    r.setLighting('pbr')
    r.setDirectionalLight(f.light)
    r.setShadows({ enabled: true, softness: 0, samples: 1 })
    r.setHybrid({ shadowSamples: 1 })
  }, fixture)
  const results = []
  for (const scene of fixture.scenes) {
    await page.evaluate(async (s) => {
      r.setRenderMode('raster')
      await r.setScene(
        s.assets.map(({ id, url, sourceSpace }) => ({ id, url, sourceSpace })),
        s.instances,
      )
      r.setRasterSettings({ pipeline: 'deferred', view: 'final' })
    }, scene)
    for (const camera of scene.cameras) {
      await page.evaluate((c) => {
        const { id, ...value } = c
        r.setViewCamera(value)
      }, camera)
      const name = scene.id + '-' + camera.id,
        captures = {}
      for (const mode of ['raster', 'hybrid']) {
        await page.evaluate(async (mode) => {
          r.setRenderMode(mode)
          await r.drawFrame()
        }, mode)
        const png = await page.locator('canvas').screenshot({ path: `${dir}/${name}-${mode}.png` })
        captures[mode] = decode(png)
      }
      const parity = difference(captures.raster, captures.hybrid)
      if (parity > 1) throw Error(name + ' Raster/Hybrid ' + parity)
      const native = {}
      for (const api of ['metal', 'vulkan']) {
        native[api] = difference(
          captures.hybrid,
          decode(readFileSync(`build/validation/hybrid-reference/${api}/${name}-hybrid.png`)),
        )
        if (native[api] > 1) throw Error(name + ' ' + api + ' MAE ' + native[api])
      }
      results.push({ scene: scene.id, camera: camera.id, parity, native })
      console.log(results.at(-1))
    }
  }
  if (errors.length) throw Error(errors.join('\n'))
  writeFileSync(
    'build/validation/hybrid-reference/web-metrics.json',
    JSON.stringify(results, null, 2),
  )
  console.log('Original three scenes / twelve fixed camera frames: WebGPU and native parity passed')
} finally {
  await browser.close()
}
