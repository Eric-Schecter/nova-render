import { chromium } from 'playwright'
import { realpathSync, writeFileSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5173'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({
      viewport: { width: 1200, height: 1000 },
      deviceScaleFactor: 2,
    }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(base + '/')
  await page.evaluate(
    async (root) => {
      const { showAO } = await import(root + '/web/examples/ao.js')
      const c = document.createElement('canvas')
      c.style = 'display:block;width:960px;height:720px'
      document.body.replaceChildren(c)
      window.scene = await showAO(c, '/generated/SponzaWhite.glb', '/generated/', undefined, {
        onError: (e) => {
          throw e
        },
      })
    },
    '/@fs' + realpathSync('.'),
  )
  await page.waitForFunction(() => window.scene.getPerformanceStats().state === 'idle')
  const initial = await page.evaluate(() => window.scene.getPerformanceStats())
  if (
    initial.fps !== null ||
    initial.lastRenderMs <= 0 ||
    initial.width !== 960 ||
    initial.height !== 720
  )
    throw Error('Invalid initial performance snapshot ' + JSON.stringify(initial))
  const bounds = await page.evaluate(() => {
    const c = document.querySelector('canvas').getBoundingClientRect(),
      h = document.querySelector('[aria-label="Rendering performance"]').getBoundingClientRect()
    return h.bottom <= c.top + 0.5
  })
  if (!bounds) throw Error('Performance bar overlaps canvas')
  let low
  for (let i = 0; i < 12; i++) {
    await page.evaluate((i) => window.scene.setCamera({ yaw: 90 + i * 0.3 }), i)
    await page.waitForTimeout(30)
    const p = await page.evaluate(() => window.scene.getPerformanceStats())
    if (p.interactive && p.scale < 0.5) low = p
  }
  if (!low || low.width !== 240 || low.height !== 180 || low.samples !== 4)
    throw Error('Adaptive preview did not activate ' + JSON.stringify(low))
  await page.waitForFunction(
    () => {
      const p = window.scene.getPerformanceStats()
      return p.state === 'idle' && p.scale === 0.5 && p.samples === 16
    },
    null,
    { timeout: 30000 },
  )
  const settled = await page.evaluate(() => window.scene.getPerformanceStats())
  if (settled.fps !== null) throw Error('Idle must not show fabricated FPS')
  const requested = await page.evaluate(() => window.scene.getAmbientOcclusion())
  if (requested.resolutionScale !== 0.5 || requested.samples !== 16)
    throw Error('Adaptive quality changed user settings')
  await page.waitForTimeout(350)
  if (
    (await page.evaluate(() => window.scene.getPerformanceStats())).frameCount !==
    settled.frameCount
  )
    throw Error('Idle renderer keeps working')
  await page.getByRole('checkbox', { name: 'Adaptive interaction quality', exact: true }).uncheck()
  await page.evaluate(() => window.scene.setCamera({ yaw: 96 }))
  await page.waitForTimeout(400)
  if ((await page.evaluate(() => window.scene.getPerformanceStats())).scale !== 0.5)
    throw Error('Adaptive toggle ignored')
  await page.evaluate(() => document.querySelector('.ao-viewport').setAttribute('inert', ''))
  const hidden = await page.evaluate(() => window.scene.getPerformanceStats())
  await page.evaluate(() =>
    window.scene.setAmbientOcclusion({ ...window.scene.getAmbientOcclusion(), method: 'rtao' }),
  )
  await page.waitForTimeout(400)
  if (
    (await page.evaluate(() => window.scene.getPerformanceStats())).frameCount !== hidden.frameCount
  )
    throw Error('Hidden AO still renders')
  if ((await page.evaluate(() => window.scene.getPerformanceStats())).state !== 'paused')
    throw Error('Hidden status missing')
  await page.evaluate(async () => {
    window.scene.setAmbientOcclusion({ ...window.scene.getAmbientOcclusion(), method: 'hbao' })
    document.querySelector('.ao-viewport').removeAttribute('inert')
    await window.scene.drawFrame()
  })
  await page
    .locator('.ao-viewport')
    .screenshot({ path: 'build/validation/ao-performance-desktop.png' })
  const metrics = await page
    .getByRole('status', { name: 'Rendering performance', exact: true })
    .textContent()
  if (!metrics.includes('FPS') || !metrics.includes('P95')) throw Error('Missing HUD metrics')
  await page.evaluate(() => window.scene.dispose())
  if (await page.locator('.ao-viewport').count()) throw Error('HUD did not dispose')
  if (errors.length) throw Error(errors.join('\n'))
  writeFileSync(
    'build/validation/ao-performance.json',
    JSON.stringify({ initial, low, settled, requested }, null, 2),
  )
  console.log(
    'PASS actual FPS/draw/P95, independent performance bar, adaptive preview/full-quality restore, unmodified settings, no idle/hidden work, toggle and disposal',
    { initial, low, settled },
  )
} finally {
  await browser.close()
}
