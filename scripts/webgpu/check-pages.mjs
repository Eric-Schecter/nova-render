import { chromium } from 'playwright'
import { mkdirSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5207/nova-render'
const names = [
  'triangle',
  'damaged-helmet',
  'lighting',
  'environment',
  'skeletal-animation',
  'integrators',
  'weekend-spheres',
  'ambient-occlusion',
  'antialiasing',
  'shadows',
  'postprocessing',
  'ssr',
  'planar',
  'probe',
  'scene',
]
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
mkdirSync('build/validation/pages', { recursive: true })
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1100 } })
  const errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  page.on('response', (r) => {
    if (r.status() >= 400) errors.push(`${r.status()} ${r.url()}`)
  })
  page.on('request', (r) => {
    if (new URL(r.url()).pathname.startsWith('/generated/') || r.url().includes('/__nova/compile'))
      errors.push('Invalid static request: ' + r.url())
  })
  for (const name of names) {
    await page.goto(`${base}/examples/${name}.html`)
    await page.locator('[data-ready="true"]').waitFor({ timeout: 120000 })
    if ((await page.locator('html').getAttribute('lang')) !== 'en-US') throw Error('Wrong language')
    if (/[\p{Script=Han}]/u.test(await page.locator('body').innerText()))
      throw Error(name + ': untranslated text')
    if ((await page.locator('[aria-label="Rendering performance"]').count()) !== 1)
      throw Error(name + ': missing performance')
    await page.locator('canvas').first().scrollIntoViewIfNeeded()
    await page.waitForTimeout(500)
    if (['triangle', 'integrators', 'probe'].includes(name))
      await page.screenshot({ path: `build/validation/pages/${name}.png` })
    await page.getByRole('button', { name: 'Hide preview', exact: true }).click()
    await page.getByRole('button', { name: 'Show preview', exact: true }).click()
    if (name === 'triangle') {
      const toggle = page.getByRole('button', { name: 'Show parameters', exact: true })
      if (await toggle.count()) await toggle.click()
      await page.getByLabel('Anti-aliasing method', { exact: true }).selectOption('fxaa')
      await page.waitForTimeout(1000)
    }
    await page.setViewportSize({ width: 390, height: 844 })
    if (await page.evaluate(() => document.documentElement.scrollWidth > innerWidth))
      throw Error(name + ': mobile overflow')
    await page.setViewportSize({ width: 1440, height: 1100 })
    console.log('PASS ' + name)
  }
  if (errors.length) throw Error([...new Set(errors)].join('\n'))
  console.log(`All ${names.length} static WebGPU examples passed`)
} finally {
  await browser.close()
}
