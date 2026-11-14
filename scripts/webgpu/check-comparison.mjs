import { chromium } from 'playwright'
import { realpathSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5173'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1100, height: 1100 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(base + '/')
  await page.evaluate(
    async (root) => {
      const { showComparison } = await import(root + '/web/examples/comparison.js')
      const host = document.createElement('div')
      host.style.cssText = 'width:640px;max-width:100%'
      document.body.replaceChildren(host)
      const scene = await showComparison(host, '/generated/CornellCaustics.glb', '/generated/', {
        yaw: 0,
        pitch: 0,
        distance: 3.6,
        exposure: 3,
      })
      window.scene = scene
      scene.setRayTracing({ ...scene.getRayTracing(), maxBounces: 8, resolutionScale: 0.5 })
      scene.setBudget({ samples: 16, milliseconds: 0 })
    },
    '/@fs' + realpathSync('.'),
  )
  await page.waitForFunction(() => window.scene.getProgress().every((p) => p.complete), null, {
    timeout: 120000,
  })
  const frozen = await page.evaluate(() => window.scene.getProgress())
  if (frozen.some((p) => p.samples !== 16)) throw Error('Budget overshot')
  await page.waitForTimeout(300)
  if (
    JSON.stringify(await page.evaluate(() => window.scene.getProgress())) !== JSON.stringify(frozen)
  )
    throw Error('Frozen scene kept rendering')
  if ((await page.locator('canvas').count()) !== 4) throw Error('Not four independent views')
  const boxes = await page.locator('canvas').evaluateAll((es) =>
    es.map((e) => {
      const b = e.getBoundingClientRect()
      return { x: b.x, y: b.y, w: b.width, h: b.height }
    }),
  )
  if (
    boxes[0].y !== boxes[1].y ||
    boxes[2].y <= boxes[0].y ||
    boxes[2].x !== boxes[0].x ||
    boxes[3].x !== boxes[1].x
  )
    throw Error('Not 2x2')
  const panel = await page.locator('.comparison-controls').boundingBox()
  if (panel.y < boxes[3].y + boxes[3].h) throw Error('Panel covers results')
  if (await page.getByRole('combobox', { name: '渲染模式', exact: true }).count())
    throw Error('Comparison is still a mode')
  const sums = []
  for (let i = 0; i < 4; i++) {
    const png = await page.locator('canvas').nth(i).screenshot()
    sums.push(
      await page.evaluate(async (base64) => {
        const image = new Image()
        image.src = 'data:image/png;base64,' + base64
        await image.decode()
        const c = document.createElement('canvas')
        c.width = image.width
        c.height = image.height
        const ctx = c.getContext('2d')
        ctx.drawImage(image, 0, 0)
        const data = ctx.getImageData(0, 0, c.width, c.height).data
        let sum = 0
        for (let k = 0; k < data.length; k += 4) sum += data[k] + data[k + 1] + data[k + 2]
        return sum / (c.width * c.height * 3)
      }, png.toString('base64')),
    )
  }
  await page
    .locator('.comparison-grid')
    .screenshot({ path: 'build/validation/comparison-example-grid.png' })
  if (sums.some((s) => !Number.isFinite(s) || s < 1 || s > 110))
    throw Error('Invalid comparison brightness ' + sums)
  await page.evaluate(() => {
    window.scene.setBudget({ samples: 3, milliseconds: 0 })
    window.scene.setCamera({ ...window.scene.getCamera(), yaw: 5 })
  })
  await page.waitForFunction(() => window.scene.getProgress().every((p) => p.complete), null, {
    timeout: 120000,
  })
  if ((await page.evaluate(() => window.scene.getProgress())).some((p) => p.samples !== 3))
    throw Error('Camera reset/budget failed')
  const oldYaw = await page.evaluate(() => window.scene.getCamera().yaw),
    tile = await page.locator('canvas').nth(3).boundingBox()
  await page.mouse.move(tile.x + 30, tile.y + 60)
  await page.mouse.down()
  await page.mouse.move(tile.x + 70, tile.y + 60, { steps: 5 })
  await page.mouse.up()
  await page.waitForFunction((yaw) => window.scene.getCamera().yaw !== yaw, oldYaw)
  await page.evaluate(() => window.scene.setBudget({ samples: 64, milliseconds: 1 }))
  await page.waitForFunction(() => window.scene.getProgress().every((p) => p.complete), null, {
    timeout: 120000,
  })
  await page.evaluate(() =>
    window.scene.setRayTracing({ ...window.scene.getRayTracing(), mltBootstrapSamples: 1024 }),
  )
  await page.evaluate(() => window.scene.loadModel('/generated/WeekendSpheres.glb'))
  await page.waitForFunction(() => window.scene.getProgress().every((p) => p.complete), null, {
    timeout: 120000,
  })
  await page.setViewportSize({ width: 390, height: 900 })
  await page.waitForTimeout(500)
  if ((await page.evaluate(() => document.documentElement.scrollWidth)) > 390)
    throw Error('Mobile overflow')
  await page.evaluate(() => window.scene.dispose())
  if (await page.locator('canvas').count()) throw Error('Dispose leaked views')
  const modeCheck = await page.evaluate(
    async (root) => {
      const Nova = await import(root + '/web/src/Nova.js')
      const c = document.createElement('canvas')
      document.body.append(c)
      const r = await Nova.createRenderer({ canvas: c })
      let rejected = false
      try {
        r.setRenderMode('integrator-comparison')
      } catch {
        rejected = true
      }
      const budgetLeaked = 'comparisonSamples' in r.getRayTracing()
      r.dispose()
      c.remove()
      return { rejected, budgetLeaked }
    },
    '/@fs' + realpathSync('.'),
  )
  if (!modeCheck.rejected || modeCheck.budgetLeaked) throw Error('Core retains comparison API')
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS example-owned comparison: four independent renderers, grid/pixels, samples/time/freeze/reset, fourth-view orbit, scene change, mobile/dispose, two-mode core',
    frozen,
  )
} finally {
  await browser.close()
}
