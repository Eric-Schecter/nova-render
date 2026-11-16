import { chromium } from 'playwright'
import { mkdirSync } from 'node:fs'
import { openExampleParameters, assertFloatingParameters } from './ExamplePanel.mjs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5206'
mkdirSync('build/validation/debuggers', { recursive: true })
const b = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await b.newPage({ viewport: { width: 1440, height: 1100 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  for (const path of [
    'ambient-occlusion',
    'antialiasing',
    'shadows',
    'postprocessing',
    'ssr',
    'planar',
    'probe',
  ]) {
    await page.goto(base + '/examples/' + path)
    await page.locator('[data-ready="true"]').waitFor({ timeout: 120000 })
    const canvas = page.locator('canvas'),
      bar = page.getByLabel('Rendering performance')
    await canvas.scrollIntoViewIfNeeded()
    if (path === 'ambient-occlusion') {
      if ((await page.getByRole('button', { name: 'Show parameters', exact: true }).count()) !== 1)
        throw Error('AO should start collapsed')
    } else if (await page.locator('.aa-drawer').evaluate((el) => el.open))
      throw Error('Debugger should start collapsed')
    await openExampleParameters(page)
    await assertFloatingParameters(page, canvas, bar)
    await page.screenshot({ path: `build/validation/debuggers/${path}-desktop.png` })
    await page.setViewportSize({ width: 390, height: 900 })
    await canvas.scrollIntoViewIfNeeded()
    await page.waitForTimeout(250)
    await assertFloatingParameters(page, canvas, bar)
    if (await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1))
      throw Error(path + ' mobile overflow')
    await page.screenshot({ path: `build/validation/debuggers/${path}-mobile.png` })
    await page.setViewportSize({ width: 1440, height: 1100 })
  }
  await page.goto(base + '/examples/integrators')
  await page.locator('[data-ready="true"]').waitFor({ timeout: 120000 })
  if (await page.locator('.aa-drawer,.ao-controls').count())
    throw Error('Integrator comparison must keep its separate controls')
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS AO through probes: collapsed floating controls, desktop/mobile bounds, metrics, integrator exception and disposal',
  )
} finally {
  await b.close()
}
