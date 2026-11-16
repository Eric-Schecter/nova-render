import { openExampleParameters, assertFloatingParameters } from './ExamplePanel.mjs'
import { chromium } from 'playwright'
import { mkdirSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5205'
mkdirSync('build/validation/probe', { recursive: true })
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1100 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(base + '/examples/probe')
  await page.locator('[data-ready="true"]').waitFor({ timeout: 120000 })
  await openExampleParameters(page)
  const canvas = page.locator('canvas'),
    panel = page.getByRole('region', { name: 'Reflection probe parameters', exact: true }),
    bar = page.getByLabel('Rendering performance')
  await canvas.scrollIntoViewIfNeeded()
  await assertFloatingParameters(page, canvas, bar)
  const a = await bar.boundingBox(),
    c = await canvas.boundingBox(),
    u = await panel.boundingBox()
  if (a.y + a.height > c.y + 1 || u.y < c.y) throw Error('Probe UI overlaps rendering')
  await page.getByRole('checkbox', { name: 'Animate', exact: true }).uncheck()
  await page.waitForFunction(
    () =>
      document
        .querySelector('[aria-label="Rendering performance"]')
        .textContent.includes('32 accumulated'),
    null,
    { timeout: 60000 },
  )
  await canvas.screenshot({ path: 'build/validation/probe/default.png' })
  await page.screenshot({ path: 'build/validation/probe/desktop.png' })
  await page
    .getByRole('combobox', { name: 'Anti-aliasing method', exact: true })
    .selectOption('off')
  await page.getByRole('checkbox', { name: 'Smooth blending', exact: true }).uncheck()
  await page.getByRole('checkbox', { name: 'Smooth blending', exact: true }).check()
  await page.getByRole('combobox', { name: 'Active probe', exact: true }).selectOption('1')
  await page.getByRole('checkbox', { name: 'Box projection', exact: true }).uncheck()
  await page.waitForTimeout(250)
  await canvas.screenshot({ path: 'build/validation/probe/uncorrected.png' })
  await page.getByRole('checkbox', { name: 'Box projection', exact: true }).check()
  for (const debug of ['reflections', 'influence', 'cubemap', 'composite']) {
    await page.getByRole('combobox', { name: 'Probe output', exact: true }).selectOption(debug)
    await page.waitForTimeout(250)
    await canvas.screenshot({ path: `build/validation/probe/${debug}.png` })
  }
  for (const [label, value] of [
    ['Reflection roughness', '.7'],
    ['Reflection intensity', '.6'],
    ['Boundary blend distance', '.3'],
    ['Probe position X', '1.3'],
    ['Probe position Y', '1.2'],
    ['Probe position Z', '1.5'],
  ]) {
    await page.getByRole('slider', { name: label, exact: true }).fill(value)
    await page.getByRole('slider', { name: label, exact: true }).dispatchEvent('input')
    await page.waitForTimeout(250)
  }
  await page.getByRole('button', { name: 'Refresh probe', exact: true }).click()
  await page.waitForTimeout(250)
  await page.getByRole('checkbox', { name: 'Local cubemap', exact: true }).uncheck()
  await page.waitForTimeout(150)
  await page.getByRole('checkbox', { name: 'Local cubemap', exact: true }).check()
  await page
    .getByRole('combobox', { name: 'Reflection environment', exact: true })
    .selectOption('studio_small_09')
  await page.waitForFunction(
    () => !document.querySelector('[aria-label="Reflection environment"]').disabled,
    null,
    { timeout: 30000 },
  )
  await page
    .getByRole('combobox', { name: 'Reflection environment', exact: true })
    .selectOption('builtin')
  await page.getByRole('checkbox', { name: 'Post-processing enabled', exact: true }).uncheck()
  await page.waitForTimeout(100)
  await page.getByRole('checkbox', { name: 'Post-processing enabled', exact: true }).check()
  for (const method of ['fxaa', 'smaa', 'msaa', 'ssaa', 'taa']) {
    await page
      .getByRole('combobox', { name: 'Anti-aliasing method', exact: true })
      .selectOption(method)
    await page.waitForTimeout(200)
  }
  await page.getByRole('button', { name: 'Hide preview', exact: true }).click()
  await page.getByRole('button', { name: 'Show preview', exact: true }).click()
  await page.setViewportSize({ width: 390, height: 900 })
  await canvas.scrollIntoViewIfNeeded()
  await page.waitForTimeout(300)
  if (await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1))
    throw Error('Mobile overflow')
  await page.screenshot({ path: 'build/validation/probe/mobile.png' })
  await page.goto(base + '/api/probe')
  await page.locator('h1').waitFor()
  if (!(await page.locator('h1').textContent()).includes('Reflection probes'))
    throw Error('Missing API docs')
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS probe page: cubemap/box correction, material and position controls, refresh, environment, six AA, post, floating UI and separate metrics, mobile and disposal',
  )
} finally {
  await browser.close()
}
