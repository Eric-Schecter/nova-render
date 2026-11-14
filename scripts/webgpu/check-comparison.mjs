import { chromium } from 'playwright'
import { resolve } from 'node:path'
import { writeFile } from 'node:fs/promises'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 650 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto('http://127.0.0.1:5173/')
  await page.evaluate(
    async (root) => {
      const Nova = await import(root + '/Nova.js'),
        canvas = document.createElement('canvas')
      canvas.style.cssText = 'width:1380px;height:460px'
      document.body.replaceChildren(canvas)
      const renderer = await Nova.createRenderer({ canvas })
      window.testRenderer = renderer
      await renderer.loadModel('/generated/CornellCaustics.glb')
      renderer.setCamera({ yaw: 0, pitch: 0, distance: 3.6, exposure: 3 })
      renderer.setRayTracing({
        ...renderer.getRayTracing(),
        comparisonSamples: 64,
        comparisonMilliseconds: 0,
        maxBounces: 8,
        photonRadius: 0.1,
        resolutionScale: 0.5,
      })
      renderer.setRenderMode('integrator-comparison')
      await renderer.drawFrame()
      renderer.enableParameterPanel()
    },
    '/@fs' + resolve('web/src'),
  )
  await page.waitForFunction(
    () =>
      window.testRenderer.getComparisonProgress().length === 3 &&
      window.testRenderer.getComparisonProgress().every((p) => p.complete),
    { timeout: 120000 },
  )
  const frozen = await page.evaluate(() => window.testRenderer.getComparisonProgress())
  if (frozen.some((p) => p.samples !== 64)) throw Error('Spp budget overshot')
  await page.waitForTimeout(200)
  const after = await page.evaluate(() => window.testRenderer.getComparisonProgress())
  if (JSON.stringify(frozen) !== JSON.stringify(after)) throw Error('Frozen render continued')
  await page.getByRole('button', { name: '收起参数面板', exact: true }).click()
  await page.screenshot({ path: 'build/validation/comparison-web.png' })
  const png = await page.locator('canvas').screenshot()
  await writeFile('build/validation/comparison-canvas.png', png)
  const columns = await page.evaluate(async (base64) => {
    const image = new Image()
    image.src = 'data:image/png;base64,' + base64
    await image.decode()
    const c = document.createElement('canvas')
    c.width = image.width
    c.height = image.height
    const ctx = c.getContext('2d')
    ctx.drawImage(image, 0, 0)
    const p = ctx.getImageData(0, 0, c.width, c.height).data
    return [0, 1, 2].map((column) => {
      let sum = 0,
        n = 0
      for (let y = 40; y < c.height; y++)
        for (
          let x = Math.floor((column * c.width) / 3) + 10;
          x < ((column + 1) * c.width) / 3 - 10;
          x++
        ) {
          sum +=
            p[(y * c.width + x) * 4] + p[(y * c.width + x) * 4 + 1] + p[(y * c.width + x) * 4 + 2]
          n += 3
        }
      return sum / n
    })
  }, png.toString('base64'))
  if (columns.some((v) => v < 5)) throw Error('Blank comparison column ' + columns)
  await page.evaluate(async () => {
    const r = window.testRenderer
    r.setRayTracing({ ...r.getRayTracing(), comparisonSamples: 3, comparisonMilliseconds: 0 })
    await r.drawFrame()
  })
  await page.waitForFunction(() =>
    window.testRenderer.getComparisonProgress().every((p) => p.complete),
  )
  if (
    (await page.evaluate(() => window.testRenderer.getComparisonProgress())).some(
      (p) => p.samples !== 3,
    )
  )
    throw Error('Nonmultiple spp failed')
  await page.evaluate(async () => {
    const r = window.testRenderer
    r.setCamera({ ...r.getCamera(), yaw: 5 })
    await r.drawFrame()
  })
  await page.waitForFunction(() =>
    window.testRenderer.getComparisonProgress().every((p) => p.complete),
  )
  await page.evaluate(async () => {
    const r = window.testRenderer
    r.setRayTracing({ ...r.getRayTracing(), comparisonMilliseconds: 20 })
    await r.drawFrame()
  })
  await page.waitForFunction(() =>
    window.testRenderer.getComparisonProgress().every((p) => p.complete),
  )
  const timed = await page.evaluate(() => window.testRenderer.getComparisonProgress())
  if (timed.some((p) => p.milliseconds < 20)) throw Error('Time budget stopped early')
  await page.evaluate(async () => {
    const r = window.testRenderer
    r.setRenderMode('ray-tracing')
    await r.drawFrame()
    r.setRenderMode('raster')
    await r.drawFrame()
    r.dispose()
  })
  if (errors.length) throw Error(errors.join('\n'))
  await writeFile(
    'build/validation/comparison-web.json',
    JSON.stringify({ frozen, timed }, null, 2),
  )
  console.log('PASS comparison samples/time/freeze/reset and mode restore', frozen, timed)
} finally {
  await browser.close()
}
