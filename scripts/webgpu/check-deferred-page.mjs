import { chromium } from 'playwright'
import { mkdirSync } from 'node:fs'
mkdirSync('build/validation/deferred', { recursive: true })
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1100 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(
    process.env.NOVA_TEST_URL || 'http://127.0.0.1:4187/nova-render/examples/deferred.html',
  )
  await page.locator('.model-example[data-ready="true"]').waitFor({ timeout: 60000 })
  const canvas = page.locator('canvas')
  await canvas.scrollIntoViewIfNeeded()
  const pipeline = page.getByRole('combobox', { name: 'Raster pipeline', exact: true }),
    view = page.getByRole('combobox', { name: 'G-buffer view', exact: true }),
    aa = page.getByRole('combobox', { name: 'Anti-aliasing method', exact: true })
  if ((await pipeline.inputValue()) !== 'deferred')
    throw Error('Example must use formal Deferred by default')
  if (!(await aa.locator('option[value="msaa"]').isDisabled()))
    throw Error('Deferred MSAA option must be disabled')
  for (const option of [
    'base-color',
    'world-normal',
    'geometric-normal',
    'linear-depth',
    'roughness',
    'metallic',
    'occlusion',
    'emissive',
    'world-position',
    'final',
  ]) {
    await view.selectOption(option)
    await page.waitForTimeout(100)
  }
  await view.selectOption('linear-depth')
  await page.getByRole('slider', { name: 'Debug range', exact: true }).fill('30')
  await page.getByRole('slider', { name: 'Debug range', exact: true }).dispatchEvent('input')
  await view.selectOption('world-normal')
  await page.waitForTimeout(200)
  await canvas.screenshot({ path: 'build/validation/deferred/page-normals.png' })
  await pipeline.selectOption('forward')
  if (!(await view.isDisabled())) throw Error('Forward must disable G-buffer views')
  await aa.selectOption('msaa')
  if (!(await pipeline.locator('option[value="deferred"]').isDisabled()))
    throw Error('MSAA must disable Deferred option')
  await aa.selectOption('off')
  await pipeline.selectOption('deferred')
  await page.waitForTimeout(200)
  const model = page.getByRole('combobox', { name: 'Model', exact: true })
  const initial = await model.inputValue(),
    alternate = await model
      .locator('option')
      .evaluateAll((options) => options.find((o) => /Avocado/.test(o.textContent))?.value)
  if (alternate) {
    await model.selectOption(alternate)
    await page.waitForFunction(() => !document.querySelector('select[aria-label="Model"]').disabled)
    await page.waitForTimeout(150)
    await model.selectOption(initial)
    await page.waitForFunction(() => !document.querySelector('select[aria-label="Model"]').disabled)
  }
  const box = await canvas.boundingBox()
  await page.mouse.move(box.x + box.width * 0.3, box.y + box.height * 0.5)
  await page.mouse.down()
  await page.mouse.move(box.x + box.width * 0.4, box.y + box.height * 0.6, { steps: 5 })
  await page.mouse.up()
  await page.mouse.wheel(0, 100)
  await page.getByRole('button', { name: 'Hide preview', exact: true }).click()
  await page.getByRole('button', { name: 'Show preview', exact: true }).click()
  await page.waitForTimeout(200)
  await page.locator('.nova-panel-toggle').click()
  await canvas.screenshot({ path: 'build/validation/deferred/page-final.png' })
  await page.setViewportSize({ width: 390, height: 844 })
  await canvas.scrollIntoViewIfNeeded()
  await page.waitForTimeout(200)
  if (await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1))
    throw Error('Narrow layout overflow')
  const status = page.locator('.model-example [role="status"]')
  if (await status.count()) throw Error(await status.innerText())
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'Formal model page: pipelines, all G-buffer views, MSAA restrictions, model switching, orbit/zoom, hide/show and narrow layout passed',
  )
} finally {
  await browser.close()
}
