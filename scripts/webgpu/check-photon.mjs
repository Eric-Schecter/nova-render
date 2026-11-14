import { chromium } from 'playwright'
import { resolve } from 'node:path'
import { writeFile, readFile } from 'node:fs/promises'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage()
  await page.goto('http://127.0.0.1:5173/')
  const result = await page.evaluate(
    async (root) => {
      const { loadGLB } = await import(root + '/ModelData.js'),
        { createPathView } = await import(root + '/PathView.js'),
        { defaultEnvironment } = await import(root + '/EnvironmentData.js')
      const device = await (await navigator.gpu.requestAdapter()).requestDevice(),
        data = await loadGLB('/generated/CornellBox.glb')
      device.pushErrorScope('validation')
      const path = await createPathView(device, data, '/generated/', { linearOutput: true })
      try {
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
            photonsPerIteration: 8192,
            photonRadius: 0.16,
          }
        async function render(integrator, spp) {
          path.reset()
          settings.integrator = integrator
          const start = performance.now()
          for (let i = 0; i < spp; i += 4)
            path.draw(null, camera, [0, 0, 0, 1], size, size, 'pbr', light, null, env, settings)
          const image = await path.readPixels()
          const values = new Float32Array(image.pixels.buffer)
          if (values.some((x) => !Number.isFinite(x) || x < 0)) throw Error('Invalid radiance')
          return { values, ms: performance.now() - start }
        }
        const reference = await render('path', 4096),
          records = []
        let photon512 = []
        function metric(p, error) {
          let s = 0,
            n = 0
          for (let y = 12; y < 43; y++)
            for (let x = 9; x < 38; x++) {
              const i = y * size + x
              s += error ? (p[i] - reference.values[i]) ** 2 : p[i]
              n++
            }
          return s / n
        }
        for (const integrator of ['path', 'bidirectional', 'photon-mapping'])
          for (const spp of [8, 32, 128, 512]) {
            const r = await render(integrator, spp)
            if (integrator === 'photon-mapping' && spp === 512) photon512 = Array.from(r.values)
            records.push({
              integrator,
              spp,
              ms: r.ms,
              mse: metric(r.values, true),
              mean: metric(r.values, false),
            })
          }
        const error = await device.popErrorScope()
        if (error) throw Error(error.message)
        return { records, referenceMean: metric(reference.values, false), photon512 }
      } finally {
        path.dispose()
        data.images.forEach((i) => i.bitmap?.close())
        device.destroy()
      }
    },
    '/@fs' + resolve('web/src'),
  )
  const native = await readFile('build/photon-native-raw.bin')
  if (
    native.readUInt32LE(0) !== 48 ||
    native.readUInt32LE(4) !== 48 ||
    result.photon512.length !== 48 * 48
  )
    throw Error('Native parity image missing')
  let sum = 0,
    max = 0
  for (let i = 0; i < 48 * 48; i++) {
    const d = result.photon512[i] - native.readFloatLE(8 + i * 4)
    sum += d * d
    max = Math.max(max, Math.abs(d))
  }
  result.nativeParity = { rms: Math.sqrt(sum / (48 * 48)), max }
  if (result.nativeParity.rms > 2e-4)
    throw Error('Native/Web photon mismatch: ' + JSON.stringify(result.nativeParity))
  delete result.photon512
  await writeFile('build/validation/photon-web.json', JSON.stringify(result, null, 2))
  console.log(result)
  for (const slug of ['damaged-helmet', 'lighting', 'environment']) {
    await page.goto('http://127.0.0.1:5173/examples/' + slug)
    await page.locator('[data-ready="true"]').waitFor({ timeout: 60000 })
    await page
      .getByRole('combobox', { name: '模型选择', exact: true })
      .selectOption('/generated/CornellGlossy.glb')
    await page.waitForFunction(() => !document.querySelector('[aria-label="模型选择"]').disabled)
    await page.getByRole('combobox', { name: '渲染模式', exact: true }).selectOption('ray-tracing')
    const integrator = page.getByRole('combobox', { name: '路径积分器', exact: true })
    await integrator.selectOption('photon-mapping')
    await page.getByRole('slider', { name: '渲染比例', exact: true }).fill('.25')
    await page.getByRole('slider', { name: '每轮光子数', exact: true }).fill('1024')
    await page.getByRole('slider', { name: '光子聚集半径', exact: true }).fill('.12')
    await page.getByRole('checkbox', { name: '降噪', exact: true }).check()
    await page.waitForFunction(
      () =>
        Number(document.querySelector('[aria-label="累计采样"]')?.textContent.split(':')[1]) >= 4,
    )
    await integrator.selectOption('path')
    if (await page.getByRole('slider', { name: '每轮光子数', exact: true }).isVisible())
      throw Error('Photon controls visible in PT')
    await page.getByRole('combobox', { name: '渲染模式', exact: true }).selectOption('raster')
  }
  console.log('PASS ray mode, photon controls, scratch resize and denoise in all examples')
} finally {
  await browser.close()
}
