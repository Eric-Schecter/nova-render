import { chromium } from 'playwright'
import { mkdir } from 'node:fs/promises'
import { resolve } from 'node:path'
await mkdir('build/webgpu', { recursive: true })
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 1000 } })
  const errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto('http://127.0.0.1:5173/examples/damaged-helmet')
  await page.locator('[data-ready="true"]').waitFor({ timeout: 45000 })
  const canvas = page.locator('canvas'),
    yaw = page.getByRole('slider', { name: 'Yaw', exact: true }),
    distance = page.getByRole('slider', { name: 'Distance', exact: true })
  await canvas.scrollIntoViewIfNeeded()
  const before = await canvas.screenshot(),
    box = await canvas.boundingBox()
  const x = box.x + box.width / 2,
    y = box.y + box.height / 2
  await page.mouse.move(x, y)
  await page.mouse.down()
  await page.mouse.move(x + 120, y + 45, { steps: 8 })
  await page.mouse.up()
  await page.waitForFunction(
    () => Number(document.querySelector('[aria-label="Yaw"]').value) !== 20,
  )
  if (before.equals(await canvas.screenshot())) throw Error('Drag did not change model pixels')
  const afterDrag = await yaw.inputValue()
  await page.mouse.move(x + 150, y + 65)
  if ((await yaw.inputValue()) !== afterDrag) throw Error('Rotation continued after release')
  await page.mouse.move(x, y)
  const oldDistance = Number(await distance.inputValue()),
    scroll = await page.evaluate(() => scrollY)
  await page.mouse.wheel(0, -200)
  await page.waitForFunction(
    (old) => Number(document.querySelector('[aria-label="Distance"]').value) < old,
    oldDistance,
  )
  if ((await page.evaluate(() => scrollY)) !== scroll)
    throw Error('Wheel scrolled document while over canvas')
  await canvas.evaluate((c) => {
    for (let i = 0; i < 20; i++)
      c.dispatchEvent(new WheelEvent('wheel', { deltaY: -10000, cancelable: true }))
  })
  if (Number(await distance.inputValue()) !== 1.5) throw Error('Zoom minimum failed')
  await canvas.evaluate((c) => {
    for (let i = 0; i < 20; i++)
      c.dispatchEvent(new WheelEvent('wheel', { deltaY: 10000, cancelable: true }))
  })
  if (Number(await distance.inputValue()) !== 100) throw Error('Zoom maximum failed')
  await page.getByRole('button', { name: 'Reset camera', exact: true }).click()
  if (Number(await distance.inputValue()) !== 3.6 || Number(await yaw.inputValue()) !== 20)
    throw Error('Reset did not sync controls')
  await canvas.scrollIntoViewIfNeeded()
  const resetBox = await canvas.boundingBox()
  await page.mouse.move(resetBox.x + 100, resetBox.y + 100)
  await page.mouse.down()
  await page.mouse.move(resetBox.x + 125, resetBox.y + 100)
  await page.mouse.up()
  await page.waitForFunction(
    () => Number(document.querySelector('[aria-label="Yaw"]').value) !== 20,
  )
  await page.getByRole('button', { name: 'Hide preview', exact: true }).click()
  await page.getByRole('button', { name: 'Show preview', exact: true }).click()
  await canvas.waitFor({ state: 'visible' })
  await page.screenshot({ path: 'build/webgpu/helmet-orbit.png', fullPage: true })
  // Unmount while events are pending; controller must stop rendering after disposal.
  await page.goto('http://127.0.0.1:5173/examples/triangle')
  await page.locator('[data-ready="true"]').waitFor({ timeout: 45000 })
  const cleanup = await page.evaluate(
    async (moduleURL) => {
      const { createRenderer } = await import(moduleURL)
      const c = document.createElement('canvas')
      c.style.cursor = 'crosshair'
      c.style.touchAction = 'pan-y'
      document.body.append(c)
      const r = await createRenderer({ canvas: c })
      let count = 0
      const stop = r.enableOrbitControls({ onChange: () => count++, onError: () => {} })
      r.enableOrbitControls({ onChange: () => count++, onError: () => {} })
      stop()
      c.dispatchEvent(new WheelEvent('wheel', { deltaY: 100, cancelable: true }))
      if (count !== 1) throw Error('Listeners duplicated or stale disposer removed active controls')
      r.disableOrbitControls()
      const camera = r.getCamera()
      c.dispatchEvent(new WheelEvent('wheel', { deltaY: 100, cancelable: true }))
      if (r.getCamera().distance !== camera.distance)
        throw Error('Disabled controller still active')
      r.enableOrbitControls({ onChange: () => count++, onError: () => {} })
      r.dispose()
      c.dispatchEvent(new WheelEvent('wheel', { deltaY: 100, cancelable: true }))
      const good = count === 1 && c.style.cursor === 'crosshair' && c.style.touchAction === 'pan-y'
      c.remove()
      return good
    },
    '/@fs/' + resolve('web/src/Nova.js').replaceAll('\\', '/'),
  )
  if (!cleanup) throw Error('Dispose did not restore styles/remove listeners')
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS pointer drag/GPU output, release, wheel/scroll, zoom bounds, slider reset, hide/show, enable/disable/dispose cleanup',
  )
} finally {
  await browser.close()
}
