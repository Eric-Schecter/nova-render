import { mkdirSync } from 'node:fs'
mkdirSync('build/validation/pipelines', { recursive: true })
import { chromium } from 'playwright'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({
    viewport: { width: 1440, height: 1100 },
    deviceScaleFactor: 1,
  })
  const errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(
    process.env.NOVA_TEST_URL || 'http://127.0.0.1:4187/nova-render/examples/pipelines.html',
  )
  await page.locator('.aa-playground[data-ready="true"]').waitFor({ timeout: 60000 })
  const preview = page.locator('#nova-pipelines-preview')
  await preview.scrollIntoViewIfNeeded()
  await page.locator('.pipeline-controls summary').click()
  const select = page.getByRole('combobox', { name: 'Pipeline' }),
    output = page.getByRole('combobox', { name: 'Output' })
  for (const pipeline of ['forward', 'deferred', 'forward-plus', 'clustered-forward']) {
    await select.selectOption(pipeline)
    await page.waitForTimeout(400)
  }
  for (const debug of [
    'base-color',
    'world-normal',
    'linear-depth',
    'roughness',
    'metallic',
    'light-count',
    'overflow',
    'final',
  ]) {
    await output.selectOption(debug)
    await page.waitForTimeout(250)
  }
  await page.getByRole('slider', { name: 'Lights', exact: true }).fill('256')
  await page.getByRole('slider', { name: 'Light radius', exact: true }).fill('4')
  await page.waitForTimeout(400)
  await page.locator('.pipeline-controls summary').click()
  await preview.screenshot({ path: 'build/validation/pipelines/page-desktop.png' })
  const box = await page.locator('.pipeline-viewport canvas').boundingBox()
  await page.mouse.move(box.x + box.width * 0.4, box.y + box.height * 0.5)
  await page.mouse.down()
  await page.mouse.move(box.x + box.width * 0.5, box.y + box.height * 0.6, { steps: 5 })
  await page.mouse.up()
  await page.mouse.wheel(0, 100)
  await page.waitForTimeout(300)
  await page.getByRole('button', { name: 'Hide preview', exact: true }).click()
  await page.getByRole('button', { name: 'Show preview', exact: true }).click()
  await page.waitForTimeout(200)
  await page.setViewportSize({ width: 390, height: 844 })
  await preview.scrollIntoViewIfNeeded()
  await page.waitForTimeout(400)
  await preview.screenshot({ path: 'build/validation/pipelines/page-mobile-layout.png' })
  if (await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1))
    throw Error('Horizontal page overflow')
  await page.getByRole('button', { name: 'Reset', exact: true }).click()
  await page.locator('.aa-playground[data-ready="true"]').waitFor({ timeout: 60000 })
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'All pipelines, all debug views, light controls, orbit/zoom, hide/show, narrow layout and reset passed',
  )
} finally {
  await browser.close()
}
