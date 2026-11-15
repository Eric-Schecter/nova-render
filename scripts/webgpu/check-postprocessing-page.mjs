import { openExampleParameters, assertFloatingParameters } from './ExamplePanel.mjs'
import { chromium } from 'playwright'
import { mkdirSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5202'
mkdirSync('build/validation/post', { recursive: true })
const b = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const p = await b.newPage({ viewport: { width: 1440, height: 1100 } }),
    errors = []
  p.on('pageerror', (e) => errors.push(e.message))
  await p.goto(base + '/examples/postprocessing')
  await p.locator('[data-ready="true"]').waitFor({ timeout: 120000 })
  await openExampleParameters(p)
  const canvas = p.locator('canvas'),
    panel = p.getByRole('region', { name: '后处理参数', exact: true }),
    bar = p.getByLabel('渲染性能')
  await canvas.scrollIntoViewIfNeeded()
  await assertFloatingParameters(p, canvas, bar)
  const a = await bar.boundingBox(),
    c = await canvas.boundingBox(),
    u = await panel.boundingBox()
  if (a.y + a.height > c.y + 1 || u.y < c.y) throw Error('UI overlaps render')
  for (const tone of ['linear', 'reinhard', 'aces']) {
    await p.getByRole('combobox', { name: '色调映射', exact: true }).selectOption(tone)
    await p.waitForTimeout(200)
  }
  const bloom = p.getByRole('checkbox', { name: '泛光开关', exact: true })
  await bloom.uncheck()
  await p.waitForTimeout(250)
  await canvas.screenshot({ path: 'build/validation/post/bloom-off.png' })
  await bloom.check()
  await p.waitForTimeout(300)
  await canvas.screenshot({ path: 'build/validation/post/bloom-on.png' })
  await p.getByRole('checkbox', { name: '自动曝光', exact: true }).check()
  await p.getByLabel('场景亮度', { exact: true }).fill('8')
  await p.getByLabel('场景亮度', { exact: true }).dispatchEvent('input')
  await p.waitForTimeout(600)
  if (!(await bar.textContent()).includes('Auto exposure')) throw Error('Missing exposure stats')
  await p.getByRole('checkbox', { name: '自动曝光', exact: true }).uncheck()
  await p.getByLabel('场景亮度', { exact: true }).fill('1')
  await p.getByLabel('场景亮度', { exact: true }).dispatchEvent('input')
  for (const method of ['off', 'fxaa', 'smaa', 'msaa', 'ssaa', 'taa']) {
    await p.getByRole('combobox', { name: '抗锯齿算法', exact: true }).selectOption(method)
    await p.waitForTimeout(300)
  }
  await p.waitForFunction(
    () => document.querySelector('[aria-label="渲染性能"]').textContent.includes('32 accumulated'),
    null,
    { timeout: 30000 },
  )
  await p.screenshot({ path: 'build/validation/post/desktop.png' })
  await p.getByRole('button', { name: '隐藏预览', exact: true }).click()
  await p.getByRole('button', { name: '显示预览', exact: true }).click()
  await p.setViewportSize({ width: 390, height: 900 })
  await canvas.scrollIntoViewIfNeeded()
  await p.waitForTimeout(300)
  if (await p.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1))
    throw Error('Mobile overflow')
  await p.screenshot({ path: 'build/validation/post/mobile.png' })
  await p.goto(base + '/api/postprocessing')
  await p.locator('h1').waitFor()
  if (!(await p.locator('h1').textContent()).includes('后处理')) throw Error('Missing docs')
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS HDR page controls, bloom, auto exposure, six AA, 32-frame settling, separate metrics/UI, mobile, hide/show and documentation',
  )
} finally {
  await b.close()
}
