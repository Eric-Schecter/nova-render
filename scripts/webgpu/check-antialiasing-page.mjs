import { openExampleParameters, assertFloatingParameters } from './ExamplePanel.mjs'
import { chromium } from 'playwright'
import { mkdirSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5198'
mkdirSync('build/validation/aa', { recursive: true })
const b = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const p = await b.newPage({ viewport: { width: 1440, height: 1100 } }),
    errors = []
  p.on('pageerror', (e) => errors.push(e.message))
  await p.goto(base + '/examples/antialiasing')
  await p.locator('[data-ready="true"]').waitFor({ timeout: 120000 })
  await openExampleParameters(p)
  const bar = p.locator('[aria-label="Rendering performance"]'),
    canvas = p.locator('canvas'),
    panel = p.getByRole('region', { name: 'Anti-aliasing parameters', exact: true }),
    methods = p.getByRole('combobox', { name: 'Anti-aliasing method', exact: true })
  await canvas.scrollIntoViewIfNeeded()
  await assertFloatingParameters(p, canvas, bar)
  if ((await methods.locator('option').count()) !== 6) throw Error('Missing AA method')
  const assertLayout = async () => {
    const a = await bar.boundingBox(),
      c = await canvas.boundingBox(),
      u = await panel.boundingBox()
    if (a.y + a.height > c.y + 1 || u.y < c.y) throw Error('UI overlaps rendered image')
  }
  await assertLayout()
  await p.getByRole('checkbox', { name: 'Animate', exact: true }).uncheck()
  for (const method of ['off', 'fxaa', 'smaa', 'msaa', 'ssaa', 'taa']) {
    await methods.selectOption(method)
    await canvas.scrollIntoViewIfNeeded()
    await p.waitForFunction(
      (method) => {
        const t = document.querySelector('[aria-label="Rendering performance"]').textContent
        return t.toLowerCase().startsWith(method) && t.includes('Idle · on demand')
      },
      method,
      { timeout: 30000 },
    )
    if (['msaa', 'ssaa'].includes(method) && !(await bar.textContent()).includes('4 samples'))
      throw Error('Multisample metadata incorrect')
    await canvas.screenshot({ path: `build/validation/aa/page-${method}.png` })
  }
  if (!(await bar.textContent()).includes('32 accumulated'))
    throw Error('Paused TAA did not settle to 32 frames')
  await p.getByRole('button', { name: 'Reset history', exact: true }).click()
  await canvas.scrollIntoViewIfNeeded()
  await p.waitForFunction(
    () =>
      document
        .querySelector('[aria-label="Rendering performance"]')
        .textContent.includes('32 accumulated'),
    null,
    { timeout: 30000 },
  )
  await p.getByRole('combobox', { name: 'Zoom', exact: true }).selectOption('4')
  await p.getByRole('slider', { name: 'History weight', exact: true }).fill('0.85')
  await p.getByRole('checkbox', { name: 'Split comparison', exact: true }).uncheck()
  await p.getByRole('checkbox', { name: 'Split comparison', exact: true }).check()
  await p.getByRole('button', { name: 'Hide preview', exact: true }).click()
  if ((await bar.isVisible()) || (await panel.isVisible()))
    throw Error('Hidden preview UI remains visible')
  await p.waitForTimeout(300)
  await p.getByRole('button', { name: 'Show preview', exact: true }).click()
  await canvas.scrollIntoViewIfNeeded()
  await assertLayout()
  await p.getByRole('checkbox', { name: 'Animate', exact: true }).check()
  await canvas.scrollIntoViewIfNeeded()
  await p.waitForFunction(
    () =>
      /\d+\.\d FPS/.test(
        document.querySelector('[aria-label="Rendering performance"]').textContent,
      ),
    null,
    { timeout: 30000 },
  )
  await p.getByRole('tab', { name: 'C++ example', exact: true }).click()
  if (!(await p.locator('.aa-source').textContent()).includes('AntiAliasingView'))
    throw Error('Missing C++ source')
  await p.getByRole('tab', { name: 'HLSL shader', exact: true }).click()
  if (!(await p.locator('.aa-source').textContent()).includes('oldMotionTex'))
    throw Error('Missing shared HLSL source')
  await bar.scrollIntoViewIfNeeded()
  await p.screenshot({ path: 'build/validation/aa/final-desktop.png' })
  await p.setViewportSize({ width: 390, height: 844 })
  await bar.scrollIntoViewIfNeeded()
  if ((await p.evaluate(() => document.documentElement.scrollWidth)) > 390)
    throw Error('Mobile overflow')
  await assertLayout()
  await p.screenshot({ path: 'build/validation/aa/final-mobile.png', fullPage: true })
  await p.getByRole('button', { name: 'Reset', exact: true }).click()
  await p.locator('[data-ready="true"]').waitFor({ timeout: 120000 })
  await openExampleParameters(p)
  if ((await bar.count()) !== 1) throw Error('Reset leaks performance strip')
  await p.goto(base + '/api/antialiasing')
  await p.locator('h1').waitFor()
  if (!(await p.locator('h1').textContent()).includes('Anti-aliasing'))
    throw Error('Wrong API page')
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS ninth AA page: six modes, motion/settling, 32-frame reset, split/zoom/weight, hide/show, FPS, source tabs, independent UI, mobile, reset/disposal and API documentation',
  )
} finally {
  await b.close()
}
