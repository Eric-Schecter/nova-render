import { chromium } from 'playwright'
import { realpathSync, mkdirSync, writeFileSync } from 'node:fs'
mkdirSync('build/validation/caustic-audit', { recursive: true })
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage()
  await page.goto(process.env.NOVA_TEST_URL || 'http://127.0.0.1:5220')
  const result = await page.evaluate(
    async (root) => {
      const { loadGLB } = await import(root + '/web/src/ModelData.js')
      const { packMaterials } = await import(root + '/web/src/MaterialData.js')
      const { createPathView, defaultPathTracing } = await import(root + '/web/src/PathView.js')
      const { defaultEnvironment } = await import(root + '/web/src/EnvironmentData.js')
      const { copySceneLights } = await import(root + '/web/src/Lighting.js')
      const device = await (
        await navigator.gpu.requestAdapter()
      ).requestDevice({ requiredLimits: { maxStorageBuffersPerShaderStage: 8 } })
      const errors = []
      device.addEventListener('uncapturederror', (e) => errors.push(e.error.message))
      const data = await loadGLB('/generated/CornellCaustics.glb')
      data.materials.forEach((m) => (m.emissive = [0, 0, 0]))
      packMaterials(data)
      const view = await createPathView(device, data, '/generated/', { linearOutput: true })
      const camera = { yaw: 0, pitch: 0, distance: 3.6, exposure: 1 },
        env = { ...defaultEnvironment(), intensity: 0, showBackground: false }
      const legacy = { direction: [0, 1, 0], color: [1, 1, 1], intensity: 0 }
      const lights = copySceneLights([
        {
          id: 'spot',
          type: 'spot',
          position: [0, 0.7, 0.2],
          direction: [0, -1, 0],
          intensity: 0.06,
          innerConeAngle: 0.55,
          outerConeAngle: 0.95,
          castShadow: true,
          shadowSoftness: 0,
        },
      ])
      const settings = {
        ...defaultPathTracing(),
        samplesPerFrame: 4,
        maxBounces: 4,
        resolutionScale: 1,
        denoise: false,
        photonsPerIteration: 8192,
        photonRadius: 0.16,
      }
      const render = async (integrator, spp) => {
        settings.integrator = integrator
        view.reset()
        let previous = 0
        const blocks = []
        for (let i = 0; i < spp; i += 4) {
          view.draw(
            null,
            camera,
            [0, 0, 0, 1],
            32,
            32,
            'pbr',
            legacy,
            null,
            env,
            settings,
            undefined,
            lights,
            { enabled: true, bias: 0.002 },
          )
          if ((i + 4) % 256 === 0) {
            const image = await view.readPixels(),
              values = new Float32Array(image.pixels.buffer)
            const mean =
              values.reduce((s, v) => {
                if (!Number.isFinite(v) || v < 0) throw Error('Invalid caustic radiance')
                return s + v
              }, 0) / values.length
            blocks.push(((i + 4) * mean - (i + 4 - 256) * previous) / 256)
            previous = mean
          }
        }
        const se = Math.sqrt(
          blocks.reduce((s, v) => s + (v - previous) ** 2, 0) /
            (blocks.length * (blocks.length - 1)),
        )
        return { integrator, spp, radius: settings.photonRadius, mean: previous, se }
      }
      const records = []
      const reference = await render('bidirectional', 32768)
      records.push(reference)
      const compatible = (r) => {
        r.relativeDifference = Math.abs(r.mean / reference.mean - 1)
        if (
          Math.abs(r.mean - reference.mean) >
          0.03 * reference.mean + 4 * Math.hypot(r.se, reference.se)
        )
          throw Error('Caustic confidence bound failed ' + JSON.stringify(r))
        records.push(r)
      }
      compatible(await render('path', 32768))
      for (const radius of [0.03, 0.16, 0.32]) {
        settings.photonRadius = radius
        compatible(await render('photon-mapping', 4096))
      }
      view.dispose()
      await device.queue.onSubmittedWorkDone()
      device.destroy()
      if (errors.length) throw Error(errors.join('\n'))
      return records
    },
    '/@fs' + realpathSync('.'),
  )
  writeFileSync('build/validation/caustic-audit/web-energy.json', JSON.stringify(result, null, 2))
  console.log('PASS WebGPU PT/BDPT statistical agreement and photon radius sweep', result)
} finally {
  await browser.close()
}
