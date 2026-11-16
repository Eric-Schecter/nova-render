import { chromium } from 'playwright'
import { realpathSync, readFileSync, writeFileSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5173',
  native = JSON.parse(readFileSync(process.argv[2] || 'build/validation/ao-native.json', 'utf8'))
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 1100 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(base + '/')
  const result = await page.evaluate(
    async ({ root, native }) => {
      const { loadGLB } = await import(root + '/web/src/ModelData.js'),
        { createPathView, defaultPathTracing } = await import(root + '/web/src/PathView.js')
      const device = await (await navigator.gpu.requestAdapter()).requestDevice()
      const planePositions = [
          [-2, -2, 0],
          [2, -2, 0],
          [2, 2, 0],
          [-2, -2, 0],
          [2, 2, 0],
          [-2, 2, 0],
        ],
        plane = {
          vertices: new Float32Array(
            planePositions.flatMap((p) => [...p, 1, 0, 0, 1, 0, 0, 0, 0, 1.5]),
          ),
          images: Array.from({ length: 5 }, (_, i) => ({
            width: 1,
            height: 1,
            pixels: new Uint8Array(i === 4 ? [0, 0, 0, 255] : [255, 255, 255, 255]),
          })),
          baseColor: [1, 1, 1, 1],
          material: [0, 1, 1, 1],
          emissive: [0, 0, 0, 0],
        }
      const camera = { yaw: 0, pitch: 0, distance: 3.6, exposure: 1, target: [0, 0, 0] },
        path = defaultPathTracing(),
        light = { direction: [0, 1, 0], color: [1, 1, 1], intensity: 0 },
        env = { rotation: 0, intensity: 0, backgroundIntensity: 0, showBackground: false },
        settings = {
          enabled: true,
          output: 'occlusion',
          denoise: false,
          radius: 0.4,
          samples: 32,
          resolutionScale: 1,
        }
      async function draw(view, method) {
        view.draw(null, camera, [0, 0, 0, 1], 32, 32, 'pbr', light, null, env, path, {
          ...settings,
          method,
        })
        const result = await view.readPixels()
        return Array.from(new Float32Array(result.pixels.buffer))
      }
      const flat = await createPathView(device, plane, '/generated/', { linearOutput: true })
      for (const method of ['disabled', 'ssao', 'hbao', 'msao', 'rtao']) {
        const values = await draw(flat, method)
        if (values.some((v) => !Number.isFinite(v) || Math.abs(v - 1) > 1e-6))
          throw Error('Unoccluded plane failed ' + method)
      }
      flat.dispose()
      const data = await loadGLB('/generated/CornellBox.glb'),
        view = await createPathView(device, data, '/generated/', { linearOutput: true }),
        stats = []
      let i = 0
      for (const method of ['ssao', 'hbao', 'msao', 'rtao']) {
        const values = await draw(view, method)
        if (values.some((v) => !Number.isFinite(v) || v < 0 || v > 1))
          throw Error('Invalid visibility ' + method)
        const mean = values.reduce((a, b) => a + b, 0) / values.length,
          mae =
            values.reduce((sum, v, k) => sum + Math.abs(v - native.methods[i][k]), 0) /
            values.length
        i++
        if (mean >= 0.999 || mean < 0.05 || mae > 0.01)
          throw Error('Occlusion/native parity failed ' + method + ' ' + mean + ' ' + mae)
        stats.push({ method, mean, mae })
      }
      view.dispose()
      data.images.forEach((i) => i.bitmap?.close())
      device.destroy()
      return stats
    },
    { root: '/@fs' + realpathSync('.'), native },
  )
  writeFileSync('build/validation/ao-web.json', JSON.stringify(result, null, 2))
  console.log('GPU statistics', result)
  await page.goto(base + '/examples/ambient-occlusion')
  await page.locator('[data-ready="true"]').waitFor({ timeout: 120000 })
  const canvas = page.locator('canvas'),
    panel = page.getByRole('region', { name: 'AO parameters', exact: true })
  await canvas.scrollIntoViewIfNeeded()
  const a = await canvas.boundingBox(),
    b = await panel.boundingBox()
  if (b.y < a.y + a.height) throw Error('Panel overlaps render')
  const initial = await canvas.screenshot()
  const deviation = await page.evaluate(async (base64) => {
    const image = new Image()
    image.src = 'data:image/png;base64,' + base64
    await image.decode()
    const c = document.createElement('canvas')
    c.width = image.width
    c.height = image.height
    const ctx = c.getContext('2d')
    ctx.drawImage(image, 0, 0)
    const p = ctx.getImageData(0, 0, c.width, c.height).data
    let sum = 0,
      sum2 = 0,
      n = 0
    for (let i = 0; i < p.length; i += 4) {
      sum += p[i]
      sum2 += p[i] * p[i]
      n++
    }
    return Math.sqrt(sum2 / n - (sum / n) ** 2)
  }, initial.toString('base64'))
  if (deviation < 5) throw Error('Default Sponza view is blocked or blank: ' + deviation)
  const hashes = []
  for (const method of ['disabled', 'ssao', 'hbao', 'msao', 'rtao']) {
    await page.getByRole('combobox', { name: 'AO method', exact: true }).selectOption(method)
    if (method === 'rtao') {
      await page.getByRole('slider', { name: 'Render scale', exact: true }).fill('.25')
      await page.waitForFunction(
        () =>
          document.querySelector('[aria-label="AO accumulation"]').textContent ===
          'Frames: 32 / 32',
        null,
        { timeout: 120000 },
      )
    }
    await canvas.screenshot({ path: `build/validation/ao-${method}.png` })
    hashes.push((await canvas.screenshot()).toString('base64').slice(-1500))
  }
  if (new Set(hashes).size < 4) throw Error('AO variants look identical')
  for (const output of ['occlusion', 'depth', 'normals', 'composite']) {
    await page.getByRole('combobox', { name: 'AO output', exact: true }).selectOption(output)
    await page.waitForTimeout(100)
  }
  await page.getByRole('combobox', { name: 'AO camera', exact: true }).selectOption('arcade')
  await page.getByRole('combobox', { name: 'AO method', exact: true }).selectOption('hbao')
  await canvas.screenshot({ path: 'build/validation/ao-doorway.png' })
  await page.getByRole('checkbox', { name: 'AO filtering', exact: true }).uncheck()
  await page.getByRole('button', { name: 'Collapse parameters', exact: true }).click()
  if (await page.getByRole('combobox', { name: 'AO method', exact: true }).isVisible())
    throw Error('Panel did not collapse')
  await page.getByRole('button', { name: 'Show parameters', exact: true }).click()
  await page.getByRole('button', { name: 'Hide preview', exact: true }).click()
  if (await panel.isVisible()) throw Error('Panel did not hide')
  await page.getByRole('button', { name: 'Show preview', exact: true }).click()
  await page.setViewportSize({ width: 390, height: 844 })
  if ((await page.evaluate(() => document.documentElement.scrollWidth)) > 390)
    throw Error('Mobile overflow')
  await page.getByRole('tab', { name: 'C++ example', exact: true }).click()
  if (!(await page.locator('.source').textContent()).includes('SetAmbientOcclusion'))
    throw Error('Missing native API example')
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS four AO algorithms, plane/corner, native-web parity, Sponza/presets, outputs/filter, 32-frame freeze, independent panel, lifecycle and mobile',
  )
} finally {
  await browser.close()
}
