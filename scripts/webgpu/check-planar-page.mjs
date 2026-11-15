import { openExampleParameters, assertFloatingParameters } from './ExamplePanel.mjs'
import { chromium } from 'playwright'
import { mkdirSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5204'
mkdirSync('build/validation/planar', { recursive: true })
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1100 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(base + '/examples/planar')
  await page.locator('[data-ready="true"]').waitFor({ timeout: 120000 })
  await openExampleParameters(page)
  const canvas = page.locator('canvas'),
    panel = page.getByRole('region', { name: '平面反射参数', exact: true }),
    bar = page.getByLabel('渲染性能')
  await canvas.scrollIntoViewIfNeeded()
  await assertFloatingParameters(page, canvas, bar)
  const a = await bar.boundingBox(),
    c = await canvas.boundingBox(),
    u = await panel.boundingBox()
  if (a.y + a.height > c.y + 1 || u.y < c.y) throw Error('Planar controls obscure rendering')
  await page.waitForFunction(
    () => document.querySelector('[aria-label="渲染性能"]').textContent.includes('32 accumulated'),
    null,
    { timeout: 60000 },
  )
  await canvas.screenshot({ path: 'build/validation/planar/default.png' })
  await page.screenshot({ path: 'build/validation/planar/desktop.png' })
  for (const label of ['地板平面反射', '竖直镜子平面反射']) {
    await page.getByRole('checkbox', { name: label, exact: true }).uncheck()
    await page.waitForTimeout(150)
    await page.getByRole('checkbox', { name: label, exact: true }).check()
  }
  for (const [label, value] of [
    ['地板粗糙度', '.7'],
    ['竖直镜子粗糙度', '.8'],
    ['地板反射强度', '.4'],
    ['竖直镜子反射强度', '.5'],
  ]) {
    await page.getByRole('slider', { name: label, exact: true }).fill(value)
    await page.getByRole('slider', { name: label, exact: true }).dispatchEvent('input')
  }
  await page.waitForTimeout(500)
  await canvas.screenshot({ path: 'build/validation/planar/rough.png' })
  await page
    .getByRole('combobox', { name: '反射环境', exact: true })
    .selectOption('studio_small_09')
  await page.waitForFunction(
    () => !document.querySelector('[aria-label="反射环境"]').disabled,
    null,
    { timeout: 30000 },
  )
  await page.getByRole('combobox', { name: '反射环境', exact: true }).selectOption('builtin')
  await page.getByRole('checkbox', { name: '后处理开关', exact: true }).uncheck()
  await page.waitForTimeout(150)
  await page.getByRole('checkbox', { name: '后处理开关', exact: true }).check()
  for (const method of ['off', 'fxaa', 'smaa', 'msaa', 'ssaa', 'taa']) {
    await page.getByRole('combobox', { name: '抗锯齿算法', exact: true }).selectOption(method)
    await page.waitForTimeout(200)
  }
  await page.getByRole('button', { name: '隐藏预览', exact: true }).click()
  await page.getByRole('button', { name: '显示预览', exact: true }).click()
  await page.setViewportSize({ width: 390, height: 900 })
  await canvas.scrollIntoViewIfNeeded()
  await page.waitForTimeout(350)
  if (await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1))
    throw Error('Mobile overflow')
  await page.screenshot({ path: 'build/validation/planar/mobile.png' })
  await page.getByRole('tab', { name: 'C++ 示例', exact: true }).click()
  await page.goto(base + '/api/planar')
  await page.locator('h1').waitFor()
  if (!(await page.locator('h1').textContent()).includes('平面反射'))
    throw Error('Missing planar API')
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS planar page: two mirrors, roughness/intensity, environment, six AA, post, floating controls and separate metrics, mobile and disposal',
  )
} finally {
  await browser.close()
}
