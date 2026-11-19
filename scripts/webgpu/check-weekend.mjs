import { chromium } from 'playwright'
import { resolve } from 'node:path'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5173'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1200, height: 1000 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(base + '/examples/weekend-spheres')
  await page.locator('[data-ready="true"]').waitFor({ timeout: 90000 })
  if (
    (await page.getByRole('combobox', { name: 'Render mode', exact: true }).inputValue()) !==
    'ray-tracing'
  )
    throw Error('Wrong default mode')
  await page.getByRole('button', { name: 'Collapse parameters', exact: true }).click()
  await page.locator('canvas').screenshot({ path: 'build/validation/weekend-spheres.png' })
  await page.setViewportSize({ width: 390, height: 844 })
  if ((await page.evaluate(() => document.documentElement.scrollWidth)) > 390)
    throw Error('Mobile overflow')
  await page.goto(base + '/')
  const stats = await page.evaluate(
    async (root) => {
      const Nova = await import(root + '/Nova.js'),
        { loadGLB } = await import(root + '/ModelData.js')
      const data = await loadGLB('/generated/WeekendSpheres.glb')
      const glass = data.vertices.filter((_, i) => i % 16 === 7 && data.vertices[i] > 0).length
      if (!glass || data.vertices.length / 48 < 50000)
        throw Error('Missing glass or sphere geometry')
      data.images.forEach((i) => i.bitmap?.close())
      const canvas = document.createElement('canvas')
      canvas.style.cssText = 'display:block;width:360px;height:360px'
      document.body.replaceChildren(canvas)
      const r = await Nova.createRenderer({ canvas })
      window.testRenderer = r
      await r.loadModel('/generated/WeekendSpheres.glb')
      r.setLighting('pbr')
      r.setCamera({ yaw: 25, pitch: 12, distance: 2.8, exposure: 1 })
      await r.drawFrame()
      r.setRayTracing({
        ...r.getRayTracing(),
        resolutionScale: 0.25,
        mltBootstrapSamples: 1024,
        maxBounces: 4,
        photonsPerIteration: 1024,
      })
      r.dispose()
      const { showComparison } = await import(
        root.replace(/\/web\/src$/, '') + '/web/examples/comparison.js'
      )
      const host = document.createElement('div')
      host.style.width = '360px'
      document.body.replaceChildren(host)
      const scene = await showComparison(host, '/generated/WeekendSpheres.glb', '/generated/', {
        yaw: 25,
        pitch: 12,
        distance: 2.8,
        exposure: 1,
      })
      scene.setRayTracing({
        ...scene.getRayTracing(),
        resolutionScale: 0.25,
        mltBootstrapSamples: 1024,
        maxBounces: 4,
        photonsPerIteration: 1024,
      })
      scene.setBudget({ samples: 4, milliseconds: 0 })
      window.scene = scene
      return { triangles: data.vertices.length / 48, glassVertices: glass }
    },
    '/@fs' + resolve('web/src'),
  )
  await page.waitForFunction(
    () =>
      window.scene.getProgress().length === 4 &&
      window.scene.getProgress().every((p) => p.complete),
    null,
    { timeout: 120000 },
  )
  const bounds = await page.evaluate(() => {
    const c = document.querySelector('canvas').getBoundingClientRect(),
      p = document.querySelector('.comparison-controls').getBoundingClientRect()
    return p.top >= c.bottom
  })
  if (!bounds) throw Error('Panel overlaps scene')
  await page
    .locator('.comparison-grid')
    .screenshot({ path: 'build/validation/weekend-comparison.png' })
  await page.evaluate(() => window.scene.dispose())
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS Weekend Spheres page, ray/raster/comparison, glass geometry, mobile bounds and separate controls',
    stats,
  )
} finally {
  await browser.close()
}
