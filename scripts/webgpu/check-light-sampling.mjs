import { chromium } from 'playwright'
import { resolve } from 'node:path'
import { writeFile } from 'node:fs/promises'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 1000 } })
  page.setDefaultTimeout(120000)
  await page.goto('http://127.0.0.1:5173/')
  const result = await page.evaluate(
    async (root) => {
      const { loadGLB, emptyModelData } = await import(root + '/ModelData.js'),
        { createPathView } = await import(root + '/PathView.js'),
        { defaultEnvironment } = await import(root + '/EnvironmentData.js')
      const device = await (await navigator.gpu.requestAdapter()).requestDevice(),
        data = await loadGLB('/generated/CornellBox.glb')
      device.pushErrorScope('validation')
      let path
      try {
        path = await createPathView(device, data, '/generated/', { linearOutput: true })
        const size = 64,
          camera = { yaw: 0, pitch: 0, distance: 3.6, exposure: 1 },
          light = { direction: [0, 1, 0], color: [1, 1, 1], intensity: 0 },
          env = { ...defaultEnvironment(), intensity: 0, showBackground: false },
          settings = {
            samplesPerFrame: 4,
            maxBounces: 5,
            resolutionScale: 1,
            denoise: false,
            lightSampling: true,
          }
        async function render(mis, spp) {
          settings.lightSampling = mis
          path.reset()
          for (let i = 0; i < spp; i += 4)
            path.draw(null, camera, [0, 0, 0, 1], size, size, 'pbr', light, null, env, settings)
          const { pixels } = await path.readPixels(),
            values = new Float32Array(pixels.buffer)
          if (values.some((x) => !Number.isFinite(x) || x < 0))
            throw Error('Invalid linear radiance')
          return values
        }
        const baseline8 = await render(false, 8),
          baseline32 = await render(false, 32),
          mis8 = await render(true, 8),
          mis32 = await render(true, 32),
          mis128 = await render(true, 128),
          reference = await render(true, 1024),
          baselineReference = await render(false, 1024)
        function statistic(pixels, error = false) {
          let sum = 0,
            count = 0
          for (let y = Math.floor(size / 4); y < Math.floor((size * 9) / 10); y++)
            for (let x = Math.floor(size / 5); x < Math.floor((size * 4) / 5); x++) {
              const i = y * size + x
              sum += error ? (pixels[i] - reference[i]) ** 2 : pixels[i]
              count++
            }
          return sum / count
        }
        const mse = (p) => statistic(p, true),
          energy = Math.abs(statistic(reference) / statistic(baselineReference) - 1),
          stats = {
            bsdf8: mse(baseline8),
            bsdf32: mse(baseline32),
            mis8: mse(mis8),
            mis32: mse(mis32),
            mis128: mse(mis128),
            meanBSDF: statistic(baselineReference),
            meanMIS: statistic(reference),
            energyDifference: energy,
          }
        if (
          !(
            stats.mis8 < stats.bsdf8 * 0.6 &&
            stats.mis32 < stats.bsdf32 * 0.6 &&
            stats.mis128 < stats.mis32 &&
            energy < 0.1
          )
        )
          throw Error('MIS convergence/energy: ' + JSON.stringify(stats))
        // Equal-SPP visual comparison in actual RGB output, with no denoising.
        path.dispose()
        path = await createPathView(device, data, '/generated/')
        const images = {}
        for (const [name, mis, spp] of [
          ['bsdf-8', false, 8],
          ['mis-8', true, 8],
          ['mis-32', true, 32],
          ['reference', true, 1024],
        ]) {
          settings.lightSampling = mis
          path.reset()
          camera.exposure = 3
          for (let i = 0; i < spp; i += 4)
            path.draw(null, camera, [0, 0, 0, 1], 128, 128, 'pbr', light, null, env, settings)
          const { pixels } = await path.readPixels(),
            canvas = document.createElement('canvas')
          canvas.width = canvas.height = 128
          canvas
            .getContext('2d')
            .putImageData(new ImageData(new Uint8ClampedArray(pixels), 128, 128), 0, 0)
          images[name] = canvas.toDataURL().split(',')[1]
        }
        path.dispose()
        const diffuse = emptyModelData()
        diffuse.vertices = new Float32Array([
          -10, -10, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 10, -10, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 10, 0,
          1, 0, 0, 1, 0, 0, 0, 0, 0,
        ])
        diffuse.material = [0, 1, 1, 1]
        const pixels = new Uint16Array(6 * 16 * 16 * 4)
        for (let i = 0; i < pixels.length; i += 4) {
          pixels[i] = pixels[i + 1] = pixels[i + 2] = 0x211f
          pixels[i + 3] = 0x3c00
        }
        for (let k = 0; k < 3; k++) pixels[((4 * 16 + 8) * 16 + 8) * 4 + k] = 0x5100
        const hdr = {
          mips: [{ size: 16, pixels }],
          irradiance: Array.from({ length: 9 }, () => [0, 0, 0, 0]),
        }
        path = await createPathView(device, diffuse, '/generated/', { linearOutput: true })
        settings.maxBounces = 1
        env.intensity = 1
        const hdrStats = []
        for (const rotation of [0, 90]) {
          env.rotation = rotation
          async function sample(mis, spp) {
            path.reset()
            settings.lightSampling = mis
            for (let i = 0; i < spp; i += 4)
              path.draw(null, camera, [0, 0, 0, 1], size, size, 'pbr', light, hdr, env, settings)
            const { pixels } = await path.readPixels()
            return new Float32Array(pixels.buffer)
          }
          const brute8 = await sample(false, 8),
            mis8 = await sample(true, 8),
            ref = await sample(true, 1024),
            brute = await sample(false, 1024)
          function metric(values, error = false) {
            let sum = 0,
              count = 0
            for (let y = 16; y < 57; y++)
              for (let x = 12; x < 51; x++) {
                const i = y * size + x
                sum += error ? (values[i] - ref[i]) ** 2 : values[i]
                count++
              }
            return sum / count
          }
          const record = {
            rotation,
            bsdf8: metric(brute8, true),
            mis8: metric(mis8, true),
            energyDifference: Math.abs(metric(ref) / metric(brute) - 1),
          }
          if (!(record.mis8 < record.bsdf8 * 0.6 && record.energyDifference < 0.1))
            throw Error('HDR MIS/rotation: ' + JSON.stringify(record))
          hdrStats.push(record)
        }
        const error = await device.popErrorScope()
        if (error) throw Error(error.message)
        return { stats, images, hdrStats }
      } finally {
        path?.dispose()
        data.images.forEach((i) => i.bitmap?.close())
        device.destroy()
      }
    },
    '/@fs' + resolve('web/src'),
  )
  for (const [name, png] of Object.entries(result.images))
    await writeFile(`build/validation/cornell-web-${name}.png`, Buffer.from(png, 'base64'))
  await writeFile('build/validation/mis-web.json', JSON.stringify(result.stats, null, 2))
  console.log('PASS Cornell emissive-only MIS, raw linear convergence and energy', result.stats)
  console.log('PASS HDR importance sampling and rotation', result.hdrStats)
  // Exercise the public panel switch and reset through all model examples.
  const errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  for (const slug of ['damaged-helmet', 'lighting', 'environment']) {
    await page.goto('http://127.0.0.1:5173/examples/' + slug)
    await page.locator('[data-ready="true"]').waitFor({ timeout: 60000 })
    await page
      .getByRole('combobox', { name: 'Model', exact: true })
      .selectOption('/generated/CornellBox.glb')
    await page.waitForFunction(() => !document.querySelector('[aria-label="Model"]').disabled)
    await page
      .getByRole('combobox', { name: 'Render mode', exact: true })
      .selectOption('ray-tracing')
    const toggle = page.getByRole('checkbox', { name: 'Light sampling and MIS', exact: true })
    if (!(await toggle.isChecked())) throw Error('MIS must default on')
    await toggle.uncheck()
    await toggle.check()
    await page.waitForFunction(
      () =>
        Number(
          document.querySelector('[aria-label="Accumulated samples"]')?.textContent.split(':')[1],
        ) >= 4,
    )
    await page.getByRole('combobox', { name: 'Render mode', exact: true }).selectOption('raster')
  }
  if (errors.length) throw Error(errors.join('\n'))
  console.log('PASS public MIS panel in all three model examples')
} finally {
  await browser.close()
}
