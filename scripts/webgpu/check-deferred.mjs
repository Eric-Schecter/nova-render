import { chromium } from 'playwright'
import { fileURLToPath } from 'node:url'
import { mkdirSync, writeFileSync, readFileSync, existsSync } from 'node:fs'
const dir = 'build/validation/deferred/web'
mkdirSync(dir, { recursive: true })
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage()
  const errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(process.env.NOVA_TEST_URL || 'http://127.0.0.1:5220')
  await page.evaluate(
    async (root) => {
      window.Nova = await import(root + '/web/src/Nova.js')
      const canvas = document.createElement('canvas')
      canvas.width = 320
      canvas.height = 240
      document.body.replaceChildren(canvas)
      window.renderer = await Nova.createRenderer({ canvas })
      renderer.setContinuousRendering(false)
      renderer.setPostProcessing({
        enabled: true,
        bloom: false,
        autoExposure: false,
        toneMapping: 'aces',
      })
      await renderer.loadModel('/generated/DamagedHelmet.glb')
    },
    '/@fs' + fileURLToPath(new URL('../../', import.meta.url)).replace(/\/$/, ''),
  )
  // Capture the compositor's last presented frame; Canvas2D copying after a GPU wait
  // can race swapchain expiration and intermittently return a cleared canvas.
  async function pixels(name) {
    const png = await page.locator('canvas').screenshot({ path: `${dir}/${name}.png` })
    return page.evaluate(async (data) => {
      const bytes = Uint8Array.from(atob(data), (c) => c.charCodeAt(0)),
        bitmap = await createImageBitmap(new Blob([bytes], { type: 'image/png' }))
      const c = new OffscreenCanvas(bitmap.width, bitmap.height),
        ctx = c.getContext('2d')
      ctx.drawImage(bitmap, 0, 0)
      bitmap.close()
      return [...ctx.getImageData(0, 0, c.width, c.height).data]
    }, png.toString('base64'))
  }
  const difference = (a, b) => a.reduce((d, v, i) => d + Math.abs(v - b[i]), 0) / a.length
  const results = []
  let directParity = 0
  for (const lighting of ['unlit', 'pbr', 'instances', 'animation', 'lights-environment']) {
    await page.evaluate(async (fixture) => {
      renderer.setRasterSettings({ pipeline: 'forward', view: 'final' })
      if (fixture === 'animation') {
        await renderer.loadModel('/generated/Fox.glb')
        renderer.setAnimationState({ ...renderer.getAnimationState(), time: 0.25, playing: false })
      }
      if (fixture === 'instances' || fixture === 'lights-environment') {
        await renderer.setScene(
          [
            { id: 'red', url: '/generated/SceneRed.glb' },
            { id: 'blue', url: '/generated/SceneBlue.glb' },
          ],
          [
            {
              id: 'a',
              assetId: 'red',
              world: [0.5, 0, 0, 0, 0, 0.5, 0, 0, 0, 0, 0.5, 0, -0.7, 0, 0, 1],
            },
            {
              id: 'b',
              assetId: 'blue',
              world: [0.5, 0, 0, 0, 0, 0.5, 0, 0, 0, 0, 0.5, 0, 0.7, 0, 0, 1],
            },
          ],
        )
      }
      renderer.setLighting(fixture === 'unlit' ? 'unlit' : 'pbr')
      if (fixture === 'lights-environment') {
        renderer.setLights([
          {
            id: 'sun',
            type: 'directional',
            direction: [0.2, 1, 1],
            castShadow: true,
            shadowSoftness: 0,
          },
          {
            id: 'point',
            type: 'point',
            position: [-1, 1, 2],
            color: [1, 0.3, 0.2],
            intensity: 6,
            castShadow: true,
            shadowSoftness: 0,
          },
          {
            id: 'spot',
            type: 'spot',
            position: [1, 2, 2],
            direction: [0, -1, -1],
            color: [0.2, 0.3, 1],
            intensity: 6,
            innerConeAngle: 0.4,
            outerConeAngle: 0.8,
            castShadow: true,
            shadowSoftness: 0,
          },
        ])
        renderer.setShadows({ enabled: true, samples: 1 })
        renderer.setFog({ mode: 'linear', nearDistance: 1, farDistance: 10 })
        await renderer.loadEnvironment('/generated/studio_small_09.bin')
        const material = renderer.getMaterials('red')[0]
        await renderer.setMaterial(
          0,
          { ...material, baseColor: [0.2, 0.7, 0.3, 1], roughness: 0.4, metallic: 0.6 },
          'red',
        )
      }
    }, lighting)
    if (lighting === 'pbr') {
      await page.evaluate(async () => {
        renderer.setPostProcessing({ enabled: false })
        renderer.setAntiAliasing({ method: 'off' })
        renderer.setRasterSettings({ pipeline: 'forward', view: 'final' })
        await renderer.drawFrame()
      })
      const direct = await pixels('forward-direct')
      await page.evaluate(async () => {
        renderer.setRasterSettings({ pipeline: 'deferred' })
        await renderer.drawFrame()
      })
      directParity = difference(direct, await pixels('deferred-no-post'))
      if (directParity > 1) throw Error('Direct forward/deferred parity failed')
      await page.evaluate(() => renderer.setPostProcessing({ enabled: true }))
    }
    for (const method of ['off', 'fxaa', 'smaa', 'ssaa', 'taa']) {
      await page.evaluate(async (method) => {
        renderer.setRasterSettings({ pipeline: 'forward', view: 'final' })
        renderer.setAntiAliasing({ method })
        await renderer.drawFrame()
      }, method)
      const forward = await pixels(`forward-${lighting}-${method}`)
      await page.evaluate(async () => {
        renderer.setRasterSettings({ pipeline: 'deferred' })
        await renderer.drawFrame()
      })
      const deferred = await pixels(`deferred-${lighting}-${method}`),
        error = difference(forward, deferred)
      results.push({ lighting, method, error })
      console.log(results.at(-1))
      if (error > 1) throw Error('Forward/deferred parity failed')
    }
  }
  await page.evaluate(() => renderer.setAntiAliasing({ method: 'off' }))
  for (const view of await page.evaluate(() => Nova.gBufferViews.slice(1))) {
    await page.evaluate(async (view) => {
      renderer.setRasterSettings({ view })
      await renderer.drawFrame()
    }, view)
    const image = await pixels(view)
    if (!['emissive', 'metallic'].includes(view) && !image.some((v, i) => i % 4 !== 3 && v > 20))
      throw Error(`Empty diagnostic: ${view}`)
  }
  await page.evaluate(async () => {
    renderer.setRasterSettings({ view: 'final' })
    const camera = renderer.getViewCamera()
    renderer.setViewCamera({
      ...camera,
      projection: 'orthographic',
      orthographicHeight: 3,
      nearPlane: 0.1,
      farPlane: 60,
    })
    renderer.setViewport({ x: 24, y: 18, width: 240, height: 180 })
    renderer.setRasterSettings({ pipeline: 'forward' })
    await renderer.drawFrame()
  })
  const viewportReference = await pixels('viewport-forward')
  await page.evaluate(async () => {
    renderer.setRasterSettings({ pipeline: 'deferred' })
    await renderer.drawFrame()
  })
  if (difference(viewportReference, await pixels('viewport-deferred')) > 1)
    throw Error('Viewport/orthographic parity failed')
  await page.evaluate(async () => {
    renderer.setViewport(null)
    await renderer.setScene([], [])
    renderer.setEnvironment({ ...renderer.getEnvironment(), showBackground: false })
    await renderer.drawFrame()
  })
  const empty = await pixels('empty-scene')
  if (empty.some((v, i) => i % 4 !== 3 && v > 40))
    throw Error('Empty scene retained G-buffer geometry')
  const rejected = await page.evaluate(() => {
    try {
      renderer.setAntiAliasing({ method: 'msaa' })
      return false
    } catch {
      return true
    }
  })
  if (!rejected) throw Error('Deferred MSAA must be rejected')
  if (errors.length) throw Error(errors.join('\n'))
  const crossBackend = []
  for (const [fixture, lighting] of [
    'unlit',
    'pbr',
    'instances',
    'animation',
    'lights-environment',
  ].entries()) {
    for (const [method, id] of [
      ['off', 0],
      ['fxaa', 1],
      ['smaa', 2],
      ['ssaa', 4],
      ['taa', 5],
    ]) {
      for (const api of ['metal', 'vulkan']) {
        const native = `build/validation/deferred/${api}/deferred-${fixture}-${id}.png`
        if (!existsSync(native)) continue
        const error = await page.evaluate(
          async ([a, b]) => {
            async function pixels(data) {
              const bytes = Uint8Array.from(atob(data), (c) => c.charCodeAt(0)),
                image = await createImageBitmap(new Blob([bytes], { type: 'image/png' })),
                canvas = new OffscreenCanvas(image.width, image.height),
                ctx = canvas.getContext('2d')
              ctx.drawImage(image, 0, 0)
              image.close()
              return ctx.getImageData(0, 0, canvas.width, canvas.height).data
            }
            const x = await pixels(a),
              y = await pixels(b)
            if (x.length !== y.length) throw Error('Backend image size mismatch')
            let d = 0
            for (let i = 0; i < x.length; i++) d += Math.abs(x[i] - y[i])
            return d / x.length
          },
          [
            readFileSync(native).toString('base64'),
            readFileSync(`${dir}/deferred-${lighting}-${method}.png`).toString('base64'),
          ],
        )
        crossBackend.push({ api, fixture, method, error })
        if (error > 2)
          throw Error(`Native/Web deferred parity ${api} ${lighting} ${method}: ${error}`)
      }
    }
  }
  writeFileSync(
    'build/validation/deferred/web.json',
    JSON.stringify({ results, crossBackend, directParity }),
  )
  await page.evaluate(() => renderer.dispose())
} finally {
  await browser.close()
}
