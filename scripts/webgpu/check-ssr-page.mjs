import { chromium } from 'playwright'
import { mkdirSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5203'
mkdirSync('build/validation/ssr', { recursive: true })
const b = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const p = await b.newPage({ viewport: { width: 1440, height: 1100 } }),
    errors = []
  p.on('pageerror', (e) => errors.push(e.message))
  await p.goto(base + '/examples/ssr')
  await p.locator('[data-ready="true"]').waitFor({ timeout: 120000 })
  const canvas = p.locator('canvas'),
    panel = p.getByRole('region', { name: 'SSR 参数', exact: true }),
    bar = p.getByLabel('渲染性能')
  await canvas.scrollIntoViewIfNeeded()
  const a = await bar.boundingBox(),
    c = await canvas.boundingBox(),
    u = await panel.boundingBox()
  if (a.y + a.height > c.y + 1 || u.y < c.y + c.height - 1)
    throw Error('SSR controls overlap render')
  await p.getByRole('combobox', { name: '抗锯齿算法', exact: true }).selectOption('off')
  for (const debug of [
    'normals',
    'roughness',
    'linear-depth',
    'depth-pyramid',
    'hit-mask',
    'reflections',
    'composite',
  ]) {
    await p.getByRole('combobox', { name: '反射调试视图', exact: true }).selectOption(debug)
    await p.waitForTimeout(150)
    await canvas.screenshot({ path: `build/validation/ssr/page-${debug}.png` })
  }
  await p.getByRole('checkbox', { name: '深度金字塔', exact: true }).uncheck()
  await p.waitForTimeout(200)
  await p.getByRole('checkbox', { name: '深度金字塔', exact: true }).check()
  await p.getByRole('checkbox', { name: 'SSR 开关', exact: true }).uncheck()
  await p.waitForTimeout(200)
  await canvas.screenshot({ path: 'build/validation/ssr/page-env-only.png' })
  await p.getByRole('checkbox', { name: 'SSR 开关', exact: true }).check()
  await p.getByRole('combobox', { name: '反射环境', exact: true }).selectOption('studio_small_09')
  await p.waitForFunction(() => !document.querySelector('[aria-label="反射环境"]').disabled, null, {
    timeout: 30000,
  })
  await p.getByRole('combobox', { name: '反射环境', exact: true }).selectOption('builtin')
  await p.waitForTimeout(250)
  await p.getByRole('checkbox', { name: '后处理开关', exact: true }).uncheck()
  await p.waitForTimeout(100)
  await p.getByRole('checkbox', { name: '后处理开关', exact: true }).check()
  for (const method of ['fxaa', 'smaa', 'msaa', 'ssaa', 'taa']) {
    await p.getByRole('combobox', { name: '抗锯齿算法', exact: true }).selectOption(method)
    await p.waitForTimeout(250)
  }
  await p.waitForFunction(
    () => document.querySelector('[aria-label="渲染性能"]').textContent.includes('32 accumulated'),
    null,
    { timeout: 60000 },
  )
  await p.screenshot({ path: 'build/validation/ssr/desktop.png' })
  await p.getByRole('button', { name: '隐藏预览', exact: true }).click()
  await p.getByRole('button', { name: '显示预览', exact: true }).click()
  await p.setViewportSize({ width: 390, height: 900 })
  await canvas.scrollIntoViewIfNeeded()
  await p.waitForTimeout(350)
  if (await p.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1))
    throw Error('Mobile SSR overflow')
  await p.screenshot({ path: 'build/validation/ssr/mobile.png' })
  await p.goto(base + '/api/ssr')
  await p.locator('h1').waitFor()
  if (!(await p.locator('h1').textContent()).includes('屏幕空间反射'))
    throw Error('SSR docs missing')
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS twelfth SSR page, hierarchy/surface diagnostics, environment fallback/switching, six AA, post, metrics, nonoverlapping controls, mobile, hide/show and disposal',
  )
} finally {
  await b.close()
}
