import { chromium } from 'playwright'
import { mkdirSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5215/nova-render'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
mkdirSync('build/validation/camera', { recursive: true })
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1100 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  page.on('response', (r) => {
    if (r.status() >= 400) errors.push(r.status() + ' ' + r.url())
  })
  await page.goto(base + '/api/camera.html')
  await page.locator('[data-ready="true"]').waitFor({ timeout: 120000 })
  await page.getByRole('button', { name: 'Camera controls', exact: true }).click()
  await page.getByLabel('Projection', { exact: true }).selectOption('orthographic')
  await page.getByLabel('Inset viewport', { exact: true }).check()
  await page.waitForTimeout(500)
  if (await page.getByRole('alert').count()) throw Error(await page.getByRole('alert').innerText())
  await page.locator('canvas').scrollIntoViewIfNeeded()
  await page.screenshot({ path: 'build/validation/camera/docs.png' })
  await page.getByLabel('Near plane', { exact: true }).fill('200')
  await page.getByLabel('Near plane', { exact: true }).press('Tab')
  await page.getByRole('alert').waitFor()
  await page.getByLabel('Near plane', { exact: true }).fill('0.1')
  await page.getByLabel('Near plane', { exact: true }).press('Tab')
  await page.getByRole('alert').waitFor({ state: 'detached' })
  await page.setViewportSize({ width: 390, height: 844 })
  await page.waitForTimeout(500)
  if (await page.evaluate(() => document.documentElement.scrollWidth > innerWidth))
    throw Error('Mobile camera docs overflow')
  if (errors.length) throw Error(errors.join('\n'))
  console.log('PASS camera documentation, live controls, invalid lens recovery and mobile layout')
} finally {
  await browser.close()
}
