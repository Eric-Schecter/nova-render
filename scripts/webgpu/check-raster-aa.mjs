import { chromium } from 'playwright'
import { realpathSync } from 'node:fs'
import assert from 'node:assert/strict'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1100, height: 800 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(process.env.NOVA_TEST_URL || 'http://127.0.0.1:5200')
  const result = await page.evaluate(
    async (root) => {
      const { createRenderer } = await import(root + '/web/src/Nova.js')
      const c = document.createElement('canvas')
      c.style = 'width:240px;height:180px'
      document.body.replaceChildren(c)
      const r = await createRenderer({ canvas: c })
      r.setContinuousRendering(false)
      const shader = await (await fetch('/generated/triangle.json')).json()
      await r.setShader(shader)
      const log = []
      for (const model of [null, '/generated/DamagedHelmet.glb', '/generated/Fox.glb']) {
        if (model) {
          await r.loadModel(model)
          await r.loadEnvironment('/generated/venice_sunset.bin')
          r.setLighting('pbr')
        }
        for (const method of ['off', 'fxaa', 'smaa', 'msaa', 'ssaa', 'taa']) {
          r.setAntiAliasing({ method })
          await r.drawFrame()
          await r.drawFrame()
          if (method === 'taa' && r.getAntiAliasingHistoryFrames() !== 2)
            throw Error('TAA history ' + model)
          log.push([model, method])
        }
        r.setCamera({ yaw: r.getCamera().yaw + 2 })
        await r.drawFrame()
        if (r.getAntiAliasingHistoryFrames() !== 3) throw Error('small camera reset')
        r.setCamera({ yaw: r.getCamera().yaw + 40 })
        await r.drawFrame()
        if (r.getAntiAliasingHistoryFrames() !== 1) throw Error('camera cut missing')
      }
      r.enableParameterPanel()
      if (!document.querySelector('[aria-label="抗锯齿算法"]')) throw Error('Missing AA UI')
      r.dispose()
      return log
    },
    '/@fs' + realpathSync('.'),
  )
  assert.equal(errors.length, 0, errors.join('\n'))
  await page.goto((process.env.NOVA_TEST_URL || 'http://127.0.0.1:5200') + '/examples/lighting')
  await page.locator('[data-ready="true"]').waitFor({ timeout: 120000 })
  const select = page.getByRole('combobox', { name: '抗锯齿算法', exact: true })
  await select.scrollIntoViewIfNeeded()
  for (const method of ['fxaa', 'smaa', 'msaa', 'ssaa', 'taa', 'off']) {
    await select.selectOption(method)
    await page.waitForTimeout(600)
  }
  await select.selectOption('taa')
  await page.waitForTimeout(1500)
  await page.locator('canvas').screenshot({ path: 'build/validation/raster-aa-web.png' })
  assert.equal(errors.length, 0, errors.join('\n'))

  console.log('PASS raster AA', result)
} finally {
  await browser.close()
}
