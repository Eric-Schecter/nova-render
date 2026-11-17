import { chromium } from 'playwright'
import { realpathSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs'
const out = 'build/validation/source-lights'
mkdirSync(out, { recursive: true })
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1050, height: 1100 } })
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
      document.body.innerHTML = '<h1>Local light source transport</h1><main></main>'
      document.body.style.cssText =
        'background:#17191c;color:white;font:16px sans-serif;margin:20px'
      document.querySelector('main').style.cssText =
        'display:grid;grid-template-columns:repeat(3,1fr);gap:8px'
      const camera = { yaw: 0, pitch: 0, distance: 3.6, exposure: 1 }
      const legacy = { direction: [0, 1, 0], color: [1, 1, 1], intensity: 0 }
      const env = { ...defaultEnvironment(), intensity: 0, showBackground: false }
      const settings = {
        ...defaultPathTracing(),
        samplesPerFrame: 4,
        maxBounces: 4,
        resolutionScale: 1,
        denoise: false,
        photonsPerIteration: 8192,
        photonRadius: 0.16,
      }
      const mean = (a) =>
        a.reduce((s, v) => {
          if (!Number.isFinite(v) || v < 0) throw Error('Nonfinite source radiance')
          return s + v
        }, 0) / a.length
      const mse = (a, b) => a.reduce((s, v, i) => s + (v - b[i]) ** 2, 0) / a.length
      const blockMSE = (a, b) => {
        let total = 0
        for (let y = 0; y < 32; y += 4)
          for (let x = 0; x < 32; x += 4) {
            let delta = 0
            for (let j = 0; j < 4; j++)
              for (let i = 0; i < 4; i++) delta += a[(y + j) * 32 + x + i] - b[(y + j) * 32 + x + i]
            total += (delta / 16) ** 2
          }
        return total / 64
      }
      const records = []
      for (const scene of ['CornellBox', 'CornellCaustics']) {
        const data = await loadGLB('/generated/' + scene + '.glb')
        data.materials.forEach((m) => (m.emissive = [0, 0, 0]))
        packMaterials(data)
        const view = await createPathView(device, data, '/generated/', { linearOutput: true })
        for (const type of ['point', 'spot']) {
          const lights = copySceneLights([
            {
              id: 'source',
              type,
              position: [0, 0.7, 0.2],
              direction: [0, -1, 0],
              intensity: 0.06,
              innerConeAngle: 0.55,
              outerConeAngle: 0.95,
              castShadow: true,
              shadowSoftness: 0,
            },
          ])
          const render = async (integrator, spp) => {
            settings.integrator = integrator
            view.reset()
            for (let i = 0; i < spp; i += 4)
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
            const image = await view.readPixels()
            const values = new Float32Array(image.pixels.buffer)
            mean(values)
            return values
          }
          settings.maxBounces = 4
          const reference = await render(
            scene === 'CornellCaustics' ? 'bidirectional' : 'path',
            scene === 'CornellCaustics' ? 16384 : 2048,
          )
          const bdpt = await render('bidirectional', 1024)
          settings.photonsPerIteration = 256
          const early = await render('photon-mapping', 8)
          settings.photonsPerIteration = 8192
          const sameSppHighBudget = await render('photon-mapping', 8)
          if (mse(early, sameSppHighBudget) < 1e-14)
            throw Error('Photon budget did not change local transport')
          if (mse(bdpt, await render('path', 1024)) < 1e-14)
            throw Error('BDPT is still camera-only')
          const photons = await render('photon-mapping', scene === 'CornellCaustics' ? 1024 : 256)
          settings.maxBounces = 1
          const direct = await render('path', 256)
          settings.maxBounces = 4
          const record = {
            scene,
            type,
            referenceMean: mean(reference),
            direct: mean(direct),
            bdpt: mean(bdpt),
            photons: mean(photons),
            mseLowBudget: mse(early, reference),
            mseHighBudget: mse(photons, reference),
            blockLow: blockMSE(early, reference),
            blockHigh: blockMSE(photons, reference),
            raw: Array.from(photons),
          }
          if (!(record.referenceMean > record.direct * 1.05)) throw Error('No indirect local light')
          if (Math.abs(record.bdpt / record.referenceMean - 1) > 0.1)
            throw Error('BDPT energy mismatch ' + JSON.stringify(record))
          if (
            Math.abs(record.photons / record.referenceMean - 1) > 0.05 ||
            record.mseHighBudget >= record.mseLowBudget
          )
            throw Error('Photon convergence/energy mismatch')
          for (const [name, values] of [
            [scene === 'CornellCaustics' ? 'Reference BDPT 16k' : 'Reference PT 2k', reference],
            ['BDPT', bdpt],
            ['Photons', photons],
          ]) {
            const cell = document.createElement('section')
            const title = document.createElement('div')
            title.textContent = scene + ' · ' + type + ' · ' + name
            const canvas = document.createElement('canvas')
            canvas.width = canvas.height = 32
            canvas.style.cssText = 'width:100%;image-rendering:pixelated'
            const pixels = new Uint8ClampedArray(32 * 32 * 4)
            values.forEach((v, i) => {
              const c = Math.round(255 * (1 - Math.exp(-v * 16)) ** (1 / 2.2))
              pixels.set([c, c, c, 255], i * 4)
            })
            canvas.getContext('2d').putImageData(new ImageData(pixels, 32, 32), 0, 0)
            cell.append(title, canvas)
            document.querySelector('main').append(cell)
          }
          lights[0].range = 0.01
          for (const integrator of ['bidirectional', 'photon-mapping'])
            if (mean(await render(integrator, 8)) > 1e-8) throw Error('Range emitted light')
          lights[0].range = 0
          lights[0].enabled = false
          for (const integrator of ['bidirectional', 'photon-mapping'])
            if (mean(await render(integrator, 4)) > 1e-8)
              throw Error('Disabled source emitted light')
          records.push(record)
        }
        view.dispose()
      }
      await device.queue.onSubmittedWorkDone()
      device.destroy()
      if (errors.length) throw Error(errors.join('\n'))
      return records
    },
    '/@fs' + realpathSync('.'),
  )
  for (const record of result) {
    const native = readFileSync(
      'build/lights-040/' + record.scene + '-source-' + record.type + '.bin',
    )
    let error = 0,
      scale = 0
    record.raw.forEach((v, i) => {
      error += (v - native.readFloatLE(i * 4)) ** 2
      scale += v * v
    })
    record.nativeRelativeRMS = Math.sqrt(error / Math.max(scale, 1e-20))
    if (record.nativeRelativeRMS > 0.03)
      throw Error('Native/Web source parity mismatch ' + record.nativeRelativeRMS)
    delete record.raw
  }
  await page.screenshot({ path: out + '/comparison.png', fullPage: true })
  writeFileSync(out + '/web.json', JSON.stringify(result, null, 2))
  console.log(result)
  console.log(
    'PASS WebGPU point/spot source paths, photons, glass caustics, convergence and native parity',
  )
} finally {
  await browser.close()
}
