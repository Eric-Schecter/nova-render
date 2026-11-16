import { chromium } from 'playwright'
import { mkdirSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5216'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
mkdirSync('build/validation/picking', { recursive: true })
try {
  const page = await browser.newPage({
      viewport: { width: 1440, height: 1100 },
      deviceScaleFactor: 2,
    }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  page.on('response', (r) => {
    if (r.status() >= 400) errors.push(r.status() + ' ' + r.url())
  })
  await page.goto(base + '/api/picking.html')
  await page.locator('[data-ready="true"]').waitFor({ timeout: 120000 })
  await page.getByRole('button', { name: 'Picking controls', exact: true }).click()
  await page.getByRole('button', { name: 'Pick left', exact: true }).click()
  await page.waitForFunction(() => document.querySelector('[data-hit-id]').textContent === 'front')
  if ((await page.locator('[data-hit-distance]').textContent()) !== '5.250')
    throw Error('Wrong front distance')
  await page.getByLabel('Exclude front', { exact: true }).check()
  await page.getByRole('button', { name: 'Pick left', exact: true }).click()
  await page.waitForFunction(() => document.querySelector('[data-hit-id]').textContent === 'back')
  if ((await page.locator('[data-hit-distance]').textContent()) !== '6.700')
    throw Error('Wrong back distance')
  await page.getByLabel('Include instances', { exact: true }).selectOption('front')
  await page.getByRole('button', { name: 'Pick left', exact: true }).click()
  if ((await page.locator('[data-hit-id]').textContent()) !== '—')
    throw Error('Exclusion precedence failed')
  await page.getByLabel('Exclude front', { exact: true }).uncheck()
  await page.getByLabel('Include instances', { exact: true }).selectOption('all')
  const canvas = page.locator('canvas')
  await canvas.scrollIntoViewIfNeeded()
  let box = await canvas.boundingBox()
  // Actual pointer click using the fixed orthographic camera, in CSS coordinates at DPR=2.
  const x = box.width * (0.5 + 1.2 / (4.5 * (box.width / box.height))),
    y = box.height * 0.5
  await canvas.click({ position: { x, y } })
  await page.waitForFunction(() => document.querySelector('[data-hit-id]').textContent === 'right')
  if (!(await page.locator('[data-hit-normal]').textContent()).includes('-0.500'))
    throw Error('Mirrored rotated normal missing')
  const marker = await page.locator('.hit-marker').boundingBox()
  box = await canvas.boundingBox()
  if (
    Math.abs(marker.x + marker.width / 2 - box.x - x) > 1 ||
    Math.abs(marker.y + marker.height / 2 - box.y - y) > 1
  )
    throw Error('Hit marker does not align with pointer')
  await page.screenshot({ path: 'build/validation/picking/page.png' })
  await page.setViewportSize({ width: 390, height: 844 })
  await canvas.scrollIntoViewIfNeeded()
  await page.waitForTimeout(300)
  if (await page.evaluate(() => document.documentElement.scrollWidth > innerWidth))
    throw Error('Mobile overflow')
  await page.getByRole('button', { name: 'Picking controls', exact: true }).click()
  box = await canvas.boundingBox()
  await canvas.click({ position: { x: box.width / 2, y: 4 } })
  await page.waitForFunction(() => document.querySelector('[data-hit-id]').textContent === '—')
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS picking preview: pointer/DPI, include/exclude, hit fields, marker alignment and mobile layout',
  )
} finally {
  await browser.close()
}
