import { chromium } from 'playwright'
import { mkdirSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:4187'
mkdirSync('build/validation/performance', { recursive: true })
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
const pages = [
  'triangle',
  'damaged-helmet',
  'lighting',
  'environment',
  'skeletal-animation',
  'integrators',
  'weekend-spheres',
  'ambient-occlusion',
]
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1100 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  for (const name of pages) {
    await page.goto(base + '/examples/' + name)
    await page.locator('[data-ready="true"]').waitFor({ timeout: 120000 })
    const bar = page.locator('[aria-label="Rendering performance"]')
    if ((await bar.count()) !== 1) throw Error(name + ': missing/duplicate metrics')
    await bar.scrollIntoViewIfNeeded()
    await page.waitForFunction(() =>
      document.querySelector('[aria-label="Rendering performance"]').textContent.includes('ms'),
    )
    for (const canvas of await page.locator('canvas').all()) {
      const a = await bar.boundingBox(),
        b = await canvas.boundingBox()
      if (a.y + a.height > b.y + 1) throw Error(name + ': metrics overlap image')
    }
    if (name === 'integrators') {
      if ((await page.locator('canvas').count()) !== 4) throw Error('Lost comparison grid')
      const first = await page.locator('canvas').nth(0).boundingBox(),
        third = await page.locator('canvas').nth(2).boundingBox()
      if (third.y <= first.y) throw Error('Comparison is not 2x2')
    }
    if (name === 'skeletal-animation') {
      await page.waitForFunction(
        () =>
          /\d+\.\d FPS/.test(
            document.querySelector('[aria-label="Rendering performance"]').textContent,
          ),
        null,
        { timeout: 30000 },
      )
    }
    if (['triangle', 'damaged-helmet', 'lighting', 'environment'].includes(name)) {
      await page.waitForTimeout(450)
      if (!(await bar.textContent()).includes('— FPS')) throw Error(name + ': idle reports FPS')
    }
    await page.getByRole('button', { name: 'Hide preview', exact: true }).click()
    if (await bar.isVisible()) throw Error(name + ': metrics remain visible when preview hidden')
    await page.getByRole('button', { name: 'Show preview', exact: true }).click()
    await page.setViewportSize({ width: 390, height: 844 })
    await bar.scrollIntoViewIfNeeded()
    if ((await page.evaluate(() => document.documentElement.scrollWidth)) > 390)
      throw Error(name + ': mobile overflow')
    // Read both rectangles in one frame: scroll anchoring and the live metrics can move the page.
    const { a, b } = await page.evaluate(() => {
      const a = document
          .querySelector('[aria-label="Rendering performance"]')
          .getBoundingClientRect(),
        b = document.querySelector('canvas').getBoundingClientRect()
      return { a: { y: a.y, height: a.height }, b: { y: b.y, height: b.height } }
    })
    if (a.y + a.height > b.y + 1) throw Error(name + ': mobile overlap')
    await page.screenshot({ path: `build/validation/performance/${name}-mobile.png` })
    await page.setViewportSize({ width: 1440, height: 1100 })
    await bar.scrollIntoViewIfNeeded()
    await page.screenshot({ path: `build/validation/performance/${name}-desktop.png` })
    console.log('PASS', name, (await bar.textContent()).replace(/\s+/g, ' '))
  }
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS all eight examples: metrics, idle/animation cadence, independent strip, 2x2 comparison, hide/show, mobile and disposal during navigation',
  )
} finally {
  await browser.close()
}
