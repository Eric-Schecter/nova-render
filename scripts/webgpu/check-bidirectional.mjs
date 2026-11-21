import { chromium } from 'playwright'
import { resolve } from 'node:path'
import { writeFile } from 'node:fs/promises'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 1000 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(process.env.NOVA_TEST_URL || 'http://127.0.0.1:5173/')
  const result = await page.evaluate(
    async (root) => {
      const { loadGLB } = await import(root + '/ModelData.js'),
        { createPathView } = await import(root + '/PathView.js'),
        { defaultEnvironment } = await import(root + '/EnvironmentData.js')
      const device = await (await navigator.gpu.requestAdapter()).requestDevice(),
        data = await loadGLB('/generated/CornellBox.glb')
      device.pushErrorScope('validation')
      let path
      try {
        path = await createPathView(device, data, '/generated/', { linearOutput: true })
        const size = 48,
          camera = { yaw: 0, pitch: 0, distance: 3.6, exposure: 1 },
          light = { direction: [0, 1, 0], color: [1, 1, 1], intensity: 0 },
          env = { ...defaultEnvironment(), intensity: 0, showBackground: false },
          settings = {
            samplesPerFrame: 4,
            maxBounces: 5,
            resolutionScale: 1,
            denoise: false,
            lightSampling: true,
            integrator: 'path',
          },
          timings = []
        async function render(integrator, spp) {
          settings.integrator = integrator
          path.reset()
          await device.queue.onSubmittedWorkDone()
          const start = performance.now()
          for (let i = 0; i < spp; i += 4)
            path.draw(null, camera, [0, 0, 0, 1], size, size, 'pbr', light, null, env, settings)
          const { pixels } = await path.readPixels(),
            values = new Float32Array(pixels.buffer)
          timings.push({ integrator, spp, ms: performance.now() - start })
          if (values.some((x) => !Number.isFinite(x) || x < 0)) throw Error('Invalid radiance')
          return values
        }
        // Warm both branches; measure the subsequent actual submissions and GPU readback.
        await render('path', 4)
        await render('bidirectional', 4)
        timings.length = 0
        const pt8 = await render('path', 8),
          bdpt8 = await render('bidirectional', 8),
          pt32 = await render('path', 32),
          bdpt32 = await render('bidirectional', 32),
          pt128 = await render('path', 128),
          bdpt128 = await render('bidirectional', 128),
          reference = await render('path', 2048),
          bdptReference = await render('bidirectional', 2048)
        function metric(p, error = false) {
          let sum = 0,
            count = 0
          for (let y = Math.floor(size / 4); y < Math.floor(size * 0.9); y++)
            for (let x = Math.floor(size / 5); x < Math.floor(size * 0.8); x++) {
              const i = y * size + x
              sum += error ? (p[i] - reference[i]) ** 2 : p[i]
              count++
            }
          return sum / count
        }
        const stats = {
          pt8: metric(pt8, true),
          bdpt8: metric(bdpt8, true),
          pt32: metric(pt32, true),
          bdpt32: metric(bdpt32, true),
          bdpt128: metric(bdpt128, true),
          pt128: metric(pt128, true),
          energyDifference: Math.abs(metric(bdptReference) / metric(reference) - 1),
        }
        if (
          !(
            stats.bdpt128 < stats.bdpt32 &&
            stats.energyDifference < 0.08 &&
            stats.bdpt32 < stats.pt32
          )
        )
          throw Error('BDPT convergence/energy: ' + JSON.stringify(stats))
        const depthAudit = []
        for (const bounces of [1, 3, 8]) {
          settings.maxBounces = bounces
          const spp = bounces === 8 ? 256 : 1024,
            a = await render('path', spp),
            b = await render('bidirectional', spp),
            difference = Math.abs(metric(b) / metric(a) - 1)
          depthAudit.push({ bounces, difference })
          if (difference > 0.08)
            throw Error('BDPT path depth energy audit: ' + JSON.stringify(depthAudit))
        }
        // A real normal map exercises radiance/importance transport reciprocity.
        const normal = data.images[1]
        normal.bitmap?.close()
        delete normal.bitmap
        normal.width = normal.height = 1
        normal.pixels = new Uint8Array([160, 128, 247, 255])
        const { packMaterials } = await import(root + '/MaterialData.js')
        for (const slot of data.materials)
          slot.normalTexture = { width: 1, height: 1, pixels: new Uint8Array([160, 128, 247, 255]) }
        packMaterials(data)
        // Cornell's original atlas uses constant UVs per triangle; give each
        // triangle a real UV basis so the new normal map actually takes effect.
        for (let i = 0; i < data.vertices.length; i += 60) {
          const u = data.vertices[i + 8],
            v = data.vertices[i + 9]
          data.vertices.set([u - 0.005, v - 0.005], i + 8)
          data.vertices.set([u + 0.005, v - 0.005], i + 28)
          data.vertices.set([u, v + 0.005], i + 48)
        }
        path.dispose()
        path = await createPathView(device, data, '/generated/', { linearOutput: true })
        settings.maxBounces = 3
        const mappedPT = await render('path', 1024),
          mappedBDPT = await render('bidirectional', 1024),
          normalEnergyDifference = Math.abs(metric(mappedBDPT) / metric(mappedPT) - 1)
        if (normalEnergyDifference > 0.1)
          throw Error('Adjoint normal energy: ' + normalEnergyDifference)
        // Mix finite emission, environment and a delta light to catch double counting.
        env.intensity = 0.3
        light.intensity = 0.5
        const mixedPT = await render('path', 1024),
          mixedBDPT = await render('bidirectional', 1024),
          mixedEnergyDifference = Math.abs(metric(mixedBDPT) / metric(mixedPT) - 1)
        if (mixedEnergyDifference > 0.08)
          throw Error('Mixed light energy: ' + mixedEnergyDifference)
        env.intensity = 0
        light.intensity = 0
        // Restore the original scene for the actual color comparison.
        const original = await loadGLB('/generated/CornellBox.glb')
        path.dispose()
        path = await createPathView(device, original, '/generated/')
        settings.maxBounces = 5
        camera.exposure = 3
        const images = {}
        for (const [name, integrator, spp] of [
          ['pt-8', 'path', 8],
          ['bdpt-8', 'bidirectional', 8],
          ['pt-32', 'path', 32],
          ['bdpt-32', 'bidirectional', 32],
          ['reference', 'path', 2048],
        ]) {
          settings.integrator = integrator
          path.reset()
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
        original.images.forEach((i) => i.bitmap?.close())
        const error = await device.popErrorScope()
        if (error) throw Error(error.message)
        return { stats, timings, depthAudit, normalEnergyDifference, mixedEnergyDifference, images }
      } finally {
        path?.dispose()
        data.images.forEach((i) => i.bitmap?.close())
        device.destroy()
      }
    },
    '/@fs' + resolve('web/src'),
  )
  for (const [name, png] of Object.entries(result.images))
    await writeFile(`build/validation/bdpt-web-${name}.png`, Buffer.from(png, 'base64'))
  delete result.images
  await writeFile('build/validation/bdpt-web.json', JSON.stringify(result, null, 2))
  console.log(
    'PASS Cornell BDPT connections, area-density MIS, convergence, adjoint normals and bounded lengths',
    result,
  )
  for (const slug of ['damaged-helmet', 'lighting', 'environment']) {
    await page.goto((process.env.NOVA_TEST_URL || 'http://127.0.0.1:5173') + '/examples/' + slug)
    await page.locator('[data-ready="true"]').waitFor({ timeout: 60000 })
    await page
      .getByRole('combobox', { name: 'Model', exact: true })
      .selectOption('/generated/CornellBox.glb')
    await page.waitForFunction(() => !document.querySelector('[aria-label="Model"]').disabled)
    await page
      .getByRole('combobox', { name: 'Render mode', exact: true })
      .selectOption('ray-tracing')
    const selector = page.getByRole('combobox', { name: 'Path integrator', exact: true })
    if ((await selector.inputValue()) !== 'path') throw Error('Default integrator changed')
    await selector.selectOption('bidirectional')
    await page.getByRole('slider', { name: 'Render scale', exact: true }).fill('.25')
    await page.waitForFunction(
      () =>
        Number(
          document.querySelector('[aria-label="Accumulated samples"]')?.textContent.split(':')[1],
        ) >= 4,
    )
    const mis = page.getByRole('checkbox', { name: 'Light sampling and MIS', exact: true })
    if (!(await mis.isChecked()) || !(await mis.isDisabled()))
      throw Error('BDPT must always use MIS')
    await page.getByRole('checkbox', { name: 'Denoise', exact: true }).check()
    await selector.selectOption('path')
    await page.waitForFunction(
      () => !document.querySelector('[aria-label="Light sampling and MIS"]').disabled,
    )
    await page.getByRole('combobox', { name: 'Render mode', exact: true }).selectOption('raster')
  }
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS public PT/BDPT API, controls, denoise and raster restoration in all model examples',
  )
} finally {
  await browser.close()
}
