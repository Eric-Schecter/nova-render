import assert from 'node:assert/strict'
import { chromium } from 'playwright'
import { fileURLToPath } from 'node:url'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5280'
const source = '/@fs' + fileURLToPath(new URL('../../web/src/Nova.js', import.meta.url))
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 320, height: 240 } })
  await page.route('**/startup-aa-switch', (route) =>
    route.fulfill({
      contentType: 'text/html',
      body: '<canvas style="width:320px;height:240px"></canvas>',
    }),
  )
  await page.route('**/red.glb', (route) =>
    route.fulfill({
      path: fileURLToPath(new URL('../../assets/SceneSample/Red.glb', import.meta.url)),
    }),
  )
  await page.goto(base + '/startup-aa-switch')
  const records = await page.evaluate(async (source) => {
    const { createRenderer } = await import(source)
    const r = await createRenderer({ canvas: document.querySelector('canvas') })
    r.setContinuousRendering(false)
    await r.setScene([{ id: 'red', url: '/red.glb' }], [{ id: 'box', assetId: 'red' }])
    let builds = 0
    const original = GPUDevice.prototype.createRenderPipelineAsync
    GPUDevice.prototype.createRenderPipelineAsync = function (...args) {
      builds++
      return original.apply(this, args)
    }
    const rows = []
    const draw = async (name) => {
      const before = builds
      if ((await r.drawFrame()) !== 'rendered') throw Error('Frame skipped: ' + name)
      rows.push([name, builds - before])
    }
    for (const method of ['smaa', 'fxaa', 'msaa', 'ssaa', 'taa', 'smaa']) {
      r.setAntiAliasing({ method })
      await draw(method)
    }
    r.setRasterSettings({ pipeline: 'deferred' })
    await draw('deferred')
    r.setViewportGrid({ enabled: true })
    await draw('grid')
    r.setRenderMode('hybrid')
    await draw('hybrid')
    r.setHybrid({
      halfResolutionShadows: true,
      sparseShadowFallback: true,
      shadowTemporalReconstruction: true,
      ambientOcclusion: true,
      halfResolutionAO: true,
      reflections: true,
      halfResolutionReflections: true,
    })
    await draw('hybrid-effects')
    await draw('hybrid-effects-repeat')
    r.setRenderMode('raster')
    r.setRasterSettings({ pipeline: 'forward' })
    r.setViewportGrid({ enabled: false })
    await draw('forward-repeat')
    r.dispose()
    return rows
  }, source)
  assert.equal(records.find((x) => x[0] === 'hybrid-effects-repeat')[1], 0)
  assert.equal(records.find((x) => x[0] === 'forward-repeat')[1], 0)
  assert.equal(records.filter((x) => x[0] === 'smaa').at(-1)[1], 0)
  console.log(JSON.stringify(records))
} finally {
  await browser.close()
}
