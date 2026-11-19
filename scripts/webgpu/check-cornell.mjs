import { chromium } from 'playwright'
import { resolve } from 'node:path'
import { writeFile } from 'node:fs/promises'
const base = process.env.NOVA_PREVIEW_URL || 'http://127.0.0.1:5173'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 1000 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(base + '/')
  let result = {}
  if (!process.env.NOVA_PREVIEW_URL) {
    result = await page.evaluate(
      async (root) => {
        const { loadGLB } = await import(root + '/ModelData.js'),
          { createPathView } = await import(root + '/PathView.js'),
          { defaultEnvironment } = await import(root + '/EnvironmentData.js')
        const device = await (await navigator.gpu.requestAdapter()).requestDevice()
        device.pushErrorScope('validation')
        const data = await loadGLB('/generated/CornellBox.glb')
        let path
        try {
          if (data.vertices.length / 48 !== 32) throw Error('Cornell geometry changed')
          path = await createPathView(device, data, '/generated/')
          const camera = { yaw: 0, pitch: 0, distance: 3.6, exposure: 1 },
            light = { direction: [0, 1, 2], color: [1, 1, 1], intensity: 3 },
            settings = defaultEnvironment(),
            options = { samplesPerFrame: 4, maxBounces: 5, resolutionScale: 1, denoise: true }
          for (let i = 0; i < 8; i++)
            path.draw(
              null,
              camera,
              [0.025, 0.035, 0.055, 1],
              256,
              256,
              'pbr',
              light,
              null,
              settings,
              options,
            )
          const image = await path.readPixels()
          let red = 0,
            green = 0
          for (let i = 0; i < image.pixels.length; i += 4) {
            const [r, g, b] = image.pixels.subarray(i, i + 3)
            if (r > g * 1.6 && r > b * 1.6 && r > 40) red++
            if (g > r * 1.6 && g > b * 1.3 && g > 40) green++
          }
          if (red < 500 || green < 500 || path.samples !== 32)
            throw Error('Cornell walls or path accumulation missing')
          const canvas = document.createElement('canvas')
          canvas.width = 256
          canvas.height = 256
          canvas
            .getContext('2d')
            .putImageData(new ImageData(new Uint8ClampedArray(image.pixels), 256, 256), 0, 0)
          const error = await device.popErrorScope()
          if (error) throw Error(error.message)
          return { red, green, png: canvas.toDataURL().split(',')[1] }
        } finally {
          path?.dispose()
          data.images.forEach((i) => i.bitmap?.close())
          device.destroy()
        }
      },
      '/@fs' + resolve('web/src'),
    )
    await writeFile('build/validation/cornell-web.png', Buffer.from(result.png, 'base64'))
    delete result.png
  }
  for (const slug of ['damaged-helmet', 'lighting', 'environment']) {
    await page.goto(base + '/examples/' + slug)
    await page.locator('[data-ready="true"]').waitFor({ timeout: 60000 })
    const models = page.getByRole('combobox', { name: 'Model', exact: true })
    await models.scrollIntoViewIfNeeded()
    await models.selectOption('/generated/CornellBox.glb')
    await page.waitForFunction(() => !document.querySelector('[aria-label="Model"]').disabled)
    if ((await models.inputValue()) !== '/generated/CornellBox.glb')
      throw Error('Cornell scene selection failed')
    await page
      .getByRole('combobox', { name: 'Render mode', exact: true })
      .selectOption('ray-tracing')
    await page.getByRole('slider', { name: 'Render scale', exact: true }).fill('.25')
    await page.getByRole('checkbox', { name: 'Denoise', exact: true }).check()
    await page.waitForFunction(
      () =>
        Number(
          document.querySelector('[aria-label="Accumulated samples"]')?.textContent.split(':')[1],
        ) >= 4,
      null,
      { timeout: 60000 },
    )
    await page.getByRole('combobox', { name: 'Render mode', exact: true }).selectOption('raster')
  }
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS Cornell 32-triangle scene, red/green wall pixels, 32-spp denoised path tracing and selectors in all three examples',
    result,
  )
} finally {
  await browser.close()
}
