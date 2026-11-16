import { chromium } from 'playwright'
const base = process.env.NOVA_PREVIEW_URL || 'http://127.0.0.1:4187'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 1100 } }),
    errors = [],
    compiles = []
  page.on('pageerror', (error) => errors.push(error.message))
  page.on('request', (request) => {
    if (request.url().includes('/__nova/compile')) compiles.push(request.url())
  })
  await page.goto(base + '/examples/environment')
  await page.locator('[data-ready="true"]').waitFor({ timeout: 45000 })
  for (const name of ['studio_small_09', 'kloppenheim_06', 'venice_sunset']) {
    const url = '/generated/' + name + '.bin'
    const response = await page.request.get(base + url, {
      headers: { 'Accept-Encoding': 'br, gzip' },
    })
    if (response.headers()['content-type'] !== 'application/octet-stream')
      throw Error('Environment must use binary MIME')
    if (response.headers()['content-encoding'])
      throw Error('Local preview is dynamically compressing HDR binary data')
    const started = Date.now()
    await page.getByRole('combobox', { name: 'Environment map', exact: true }).selectOption(url)
    await page.waitForFunction(
      () =>
        document.querySelector('[aria-label="Environment map"]').getAttribute('aria-busy') ===
        'false',
    )
    console.log(name, Date.now() - started, 'ms')
  }
  await page
    .locator('.nova-viewport')
    .screenshot({ path: 'build/webgpu/hdr-background-static.png' })
  if (errors.length || compiles.length) throw Error(JSON.stringify({ errors, compiles }))
  console.log(
    'PASS static HDR sky switching, binary MIME, no dynamic Brotli and no runtime compiler',
  )
} finally {
  await browser.close()
}
