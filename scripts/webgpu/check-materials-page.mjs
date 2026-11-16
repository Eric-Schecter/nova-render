import { chromium } from 'playwright'
import { mkdirSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5221/nova-render'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
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
  await page.goto(base + '/api/materials.html')
  await page.locator('.material-example[data-ready="true"]').waitFor({ timeout: 120000 })
  await page.getByRole('button', { name: 'Material controls' }).click()
  await page.getByLabel('Material slot', { exact: true }).selectOption('1')
  await page.getByLabel('Metallic', { exact: true }).fill('.8')
  await page.getByLabel('Metallic', { exact: true }).dispatchEvent('input')
  await page.getByLabel('Roughness', { exact: true }).fill('.2')
  await page.getByLabel('Roughness', { exact: true }).dispatchEvent('input')
  await page.getByLabel('Checker texture').check()
  await page.waitForTimeout(1000)
  if (await page.getByRole('alert').count())
    throw Error(await page.getByRole('alert').allTextContents())
  await page.getByRole('button', { name: 'Material controls' }).click()
  mkdirSync('build/validation/materials', { recursive: true })
  await page.locator('.material-output').screenshot({ path: 'build/validation/materials/page.png' })
  await page.setViewportSize({ width: 390, height: 844 })
  await page.getByRole('button', { name: 'Material controls' }).click()
  if (await page.evaluate(() => document.documentElement.scrollWidth > innerWidth))
    throw Error('Mobile overflow')
  if (errors.length) throw Error(errors.join('\n'))
  console.log('PASS material docs controls, image rendering, DPR2 and mobile layout')
} finally {
  await browser.close()
}
