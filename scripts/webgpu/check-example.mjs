import { chromium } from 'playwright'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage()
  await page.goto('http://127.0.0.1:5173/examples/triangle')
  await page.locator('[data-ready="true"]').waitFor({ timeout: 45000 })
  const code = page.getByRole('textbox', { name: 'JavaScript example source' })
  const source = await code.inputValue()
  if (!source.includes('Nova.createRenderer') || source.includes('navigator.gpu'))
    throw Error('Missing public API example')
  if (!(await code.evaluate((e) => e.readOnly))) throw Error('Source view should be read-only')
  const before = await page.locator('canvas').screenshot()
  await page.getByRole('combobox').selectOption('light')
  await page.waitForFunction(
    () => document.querySelector('[aria-label="Clear color"]').value === 'light',
  )
  let after
  for (let i = 0; i < 10; i++) {
    after = await page.locator('canvas').screenshot()
    if (!before.equals(after)) break
  }
  if (before.equals(after)) throw Error('Background did not change')
  await page.setViewportSize({ width: 1450, height: 950 })
  await page.getByRole('button', { name: 'Run', exact: true }).click()
  await page.locator('[data-ready="true"]').waitFor({ timeout: 45000 })
  await page.screenshot({ path: 'build/webgpu/api-example.png', fullPage: true })
  console.log('PASS public API source, real background change, rerun and resize')
} finally {
  await browser.close()
}
