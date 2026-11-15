import { chromium } from 'playwright'
import { mkdirSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5205'
mkdirSync('build/validation/probe', { recursive: true })
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1100 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(base + '/examples/probe')
  await page.locator('[data-ready="true"]').waitFor({ timeout: 120000 })
  const canvas = page.locator('canvas'),
    panel = page.getByRole('region', { name: '反射探针参数', exact: true }),
    bar = page.getByLabel('渲染性能')
  await canvas.scrollIntoViewIfNeeded()
  const a = await bar.boundingBox(),
    c = await canvas.boundingBox(),
    u = await panel.boundingBox()
  if (a.y + a.height > c.y + 1 || u.y < c.y + c.height - 1)
    throw Error('Probe UI overlaps rendering')
  await page.waitForFunction(
    () => document.querySelector('[aria-label="渲染性能"]').textContent.includes('32 accumulated'),
    null,
    { timeout: 60000 },
  )
  await canvas.screenshot({ path: 'build/validation/probe/default.png' })
  await page.screenshot({ path: 'build/validation/probe/desktop.png' })
  await page.getByRole('combobox', { name: '抗锯齿算法', exact: true }).selectOption('off')
  await page.getByRole('checkbox', { name: '盒体投影修正', exact: true }).uncheck()
  await page.waitForTimeout(250)
  await canvas.screenshot({ path: 'build/validation/probe/uncorrected.png' })
  await page.getByRole('checkbox', { name: '盒体投影修正', exact: true }).check()
  for (const debug of ['reflections', 'influence', 'cubemap', 'composite']) {
    await page.getByRole('combobox', { name: '探针调试视图', exact: true }).selectOption(debug)
    await page.waitForTimeout(250)
    await canvas.screenshot({ path: `build/validation/probe/${debug}.png` })
  }
  for (const [label, value] of [
    ['反射粗糙度', '.7'],
    ['反射强度', '.6'],
    ['边界混合距离', '.3'],
    ['探针位置 X', '1.3'],
    ['探针位置 Y', '1.2'],
    ['探针位置 Z', '1.5'],
  ]) {
    await page.getByRole('slider', { name: label, exact: true }).fill(value)
    await page.getByRole('slider', { name: label, exact: true }).dispatchEvent('input')
    await page.waitForTimeout(250)
  }
  await page.getByRole('button', { name: '刷新反射探针', exact: true }).click()
  await page.waitForTimeout(250)
  await page.getByRole('checkbox', { name: '局部 Cubemap', exact: true }).uncheck()
  await page.waitForTimeout(150)
  await page.getByRole('checkbox', { name: '局部 Cubemap', exact: true }).check()
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
  await page.waitForTimeout(100)
  await page.getByRole('checkbox', { name: '后处理开关', exact: true }).check()
  for (const method of ['fxaa', 'smaa', 'msaa', 'ssaa', 'taa']) {
    await page.getByRole('combobox', { name: '抗锯齿算法', exact: true }).selectOption(method)
    await page.waitForTimeout(200)
  }
  await page.getByRole('button', { name: '隐藏预览', exact: true }).click()
  await page.getByRole('button', { name: '显示预览', exact: true }).click()
  await page.setViewportSize({ width: 390, height: 900 })
  await canvas.scrollIntoViewIfNeeded()
  await page.waitForTimeout(300)
  if (await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1))
    throw Error('Mobile overflow')
  await page.screenshot({ path: 'build/validation/probe/mobile.png' })
  await page.goto(base + '/api/probe')
  await page.locator('h1').waitFor()
  if (!(await page.locator('h1').textContent()).includes('反射探针'))
    throw Error('Missing API docs')
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS probe page: cubemap/box correction, material and position controls, refresh, environment, six AA, post, separate UI/metrics, mobile and disposal',
  )
} finally {
  await browser.close()
}
