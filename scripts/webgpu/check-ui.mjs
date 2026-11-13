import { chromium } from 'playwright'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage()
  await page.goto('http://127.0.0.1:5173/examples/triangle')
  const status = page.getByRole('status')
  await page.locator('[data-ready="true"]').waitFor({ timeout: 45000 })
  await page.getByRole('tab', { name: 'HLSL shader', exact: true }).click()
  const editor = page.getByRole('textbox')
  const original = await editor.inputValue()
  await editor.fill('bad shader')
  await page.getByRole('button', { name: '运行', exact: true }).click()
  await status.filter({ hasText: 'error' }).waitFor({ timeout: 30000 })
  await editor.fill(original.replace('float3(1.0, 0.25, 0.2)', 'float3(0.2, 0.25, 1.0)'))
  await page.getByRole('button', { name: '运行', exact: true }).click()
  await page.locator('[data-ready="true"]').waitFor({ timeout: 45000 })
  await page.getByRole('button', { name: '重置', exact: true }).click()
  await page.locator('[data-ready="true"]').waitFor({ timeout: 45000 })
  if ((await editor.inputValue()) !== original) throw Error('Reset failed')
  await page.screenshot({ path: 'build/webgpu/playground.png', fullPage: true })
  console.log('PASS initial draw, invalid HLSL error, edit/recompile, reset; screenshot saved')
} finally {
  await browser.close()
}
