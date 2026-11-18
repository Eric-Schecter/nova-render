import { chromium } from 'playwright'
import { mkdirSync } from 'node:fs'
const root = 'build/validation/hybrid-online'
mkdirSync(root, { recursive: true })
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1080 } }),
    errors = [],
    requests = []
  page.on('pageerror', (e) => errors.push(e.message))
  page.on('request', (r) => {
    if (r.url().endsWith('.glb')) requests.push(r.url())
  })
  page.setDefaultTimeout(120000)
  await page.goto(process.env.NOVA_TEST_URL || 'http://127.0.0.1:4187/nova-render/examples/hybrid')
  await page.locator('.model-example[data-ready="true"]').waitFor()
  const scene = page.getByLabel('Hybrid scene', { exact: true }),
    view = page.getByLabel('Hybrid camera preset', { exact: true })
  if ((await scene.inputValue()) !== 'reflections-test') throw Error('Reference default not active')
  if (requests.length !== 1 || !requests[0].includes('reflections_test.glb'))
    throw Error('Scenes were not loaded on demand: ' + requests)
  for (const id of ['reflections-test', 'shadows-test', 'global-illumination-test', 'box']) {
    await scene.selectOption(id)
    await page.waitForFunction(
      () => !document.querySelector('[aria-label="Hybrid scene"]').disabled,
    )
    if (await page.getByRole('status').count())
      throw Error(
        'Scene load error ' + id + ': ' + (await page.getByRole('status').allTextContents()),
      )
    const count = await view.locator('option').count()
    if (count !== (id === 'box' ? 1 : 4)) throw Error('Missing presets ' + id)
    const before = requests.length
    for (let n = 0; n < count; ++n) {
      await view.selectOption({ index: n })
      await page.waitForFunction(
        () => !document.querySelector('[aria-label="Hybrid camera preset"]').disabled,
      )
    }
    if (requests.length !== before) throw Error('Camera presets reloaded models')
    await page.getByRole('button', { name: 'Reset view', exact: true }).click()
    await page.waitForFunction(
      () => !document.querySelector('[aria-label="Hybrid camera preset"]').disabled,
    )
    const diffuseBounce = page.getByLabel('Diffuse bounce', { exact: true })
    if (id === 'global-illumination-test') await diffuseBounce.check()
    await page.locator('canvas').scrollIntoViewIfNeeded()
    await page.locator('canvas').screenshot({ path: root + '/' + id + '.png' })
    if (id === 'global-illumination-test') await diffuseBounce.uncheck()
    console.log('Scene/presets passed:', id)
  }
  await scene.selectOption('reflections-test')
  await page.waitForFunction(() => !document.querySelector('[aria-label="Hybrid scene"]').disabled)
  await view.selectOption('angle-1')
  await page.waitForFunction(
    () => !document.querySelector('[aria-label="Hybrid camera preset"]').disabled,
  )
  for (const label of [
    'Hybrid ambient occlusion',
    'Hybrid reflections',
    'Hybrid temporal reconstruction',
  ]) {
    await page.getByLabel(label, { exact: true }).uncheck()
    await page.getByLabel(label, { exact: true }).check()
  }
  await page.getByLabel('Render mode', { exact: true }).selectOption('raster')
  await page.getByLabel('Hybrid reflections', { exact: true }).waitFor({ state: 'detached' })
  await page.getByLabel('Render mode', { exact: true }).selectOption('hybrid')
  await page.getByLabel('Hybrid reflections', { exact: true }).waitFor()
  await page.screenshot({ path: root + '/desktop.png', animations: 'disabled' })
  await page.setViewportSize({ width: 430, height: 932 })
  await page.getByLabel('Collapse parameters', { exact: true }).click()
  await page.locator('canvas').scrollIntoViewIfNeeded()
  await page.screenshot({ path: root + '/mobile.png', animations: 'disabled' })
  if (await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1))
    throw Error('Horizontal overflow')
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'Online Hybrid reference scenes, 12 fixed cameras, lazy loads, effect controls, mode switching and responsive layout passed',
  )
} finally {
  await browser.close()
}
