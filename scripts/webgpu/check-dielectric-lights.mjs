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
      const { emptyModelData } = await import(root + '/web/src/ModelData.js')
      const { createPathView, defaultPathTracing } = await import(root + '/web/src/PathView.js')
      const { defaultEnvironment } = await import(root + '/web/src/EnvironmentData.js')
      const { copySceneLights } = await import(root + '/web/src/Lighting.js')
      const device = await (
        await navigator.gpu.requestAdapter()
      ).requestDevice({ requiredLimits: { maxStorageBuffersPerShaderStage: 8 } })
      const errors = []
      device.addEventListener('uncapturederror', (e) => errors.push(e.error.message))
      const data = emptyModelData()
      data.material = [0, 0.4, 1, 1]
      data.vertices = new Float32Array([
        -1, -1, 0, 1, 0, 0, 1, 1, 0, 0, 0, 1.5, 1, -1, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1.5, 1, 1, 0, 1,
        0, 0, 1, 1, 1, 1, 0, 1.5, -1, -1, 0, 1, 0, 0, 1, 1, 0, 0, 0, 1.5, 1, 1, 0, 1, 0, 0, 1, 1, 1,
        1, 0, 1.5, -1, 1, 0, 1, 0, 0, 1, 1, 0, 1, 0, 1.5,
      ])
      let view = await createPathView(device, data, '/generated/', { linearOutput: true })
      const camera = { yaw: 0, pitch: 0, distance: 3.6, exposure: 1 },
        env = { ...defaultEnvironment(), intensity: 0, showBackground: false }
      const legacy = { direction: [0, 0, -1], color: [1, 1, 1], intensity: 0.06 }
      const settings = {
        ...defaultPathTracing(),
        samplesPerFrame: 4,
        maxBounces: 1,
        resolutionScale: 1,
        denoise: false,
        photonsPerIteration: 256,
      }
      const shadows = { enabled: false, bias: 0.002 }
      const render = async (integrator, list) => {
        settings.integrator = integrator
        view.reset()
        for (let i = 0; i < 64; i += 4)
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
            list,
            shadows,
          )
        const im = await view.readPixels(),
          values = new Float32Array(im.pixels.buffer)
        return (
          values.reduce((s, v) => {
            if (!Number.isFinite(v) || v < 0) throw Error('Nonfinite glass energy')
            return s + v
          }, 0) / values.length
        )
      }
      const records = []
      for (const type of ['point', 'spot']) {
        const lights = copySceneLights([
          {
            id: 'behind-interface',
            type,
            position: [0, 0, -2],
            direction: [0, 0, 1],
            intensity: 0.06,
            castShadow: true,
            shadowSoftness: 0,
            innerConeAngle: 0.6,
            outerConeAngle: 1,
          },
        ])
        shadows.enabled = false
        const ref = await render('path', lights)
        if (ref < 1e-5) throw Error('Glass transmission discarded')
        shadows.enabled = true
        for (const integrator of ['path', 'bidirectional', 'photon-mapping']) {
          const value = await render(integrator, lights)
          if (Math.abs(value / ref - 1) > 0.001)
            throw Error('Interface self-shadow ' + type + ' ' + integrator)
          lights[0].intensity = 100
          const bright = await render(integrator, lights)
          lights[0].intensity = 0.06
          const linearity = Math.abs(bright / (ref * (100 / 0.06)) - 1)
          if (linearity > 0.001) throw Error('Hidden radiance clamp ' + integrator)
          records.push({ type, integrator, reference: ref, shadowed: value, bright, linearity })
        }
      }
      if ((await render('path', null)) < 1e-5)
        throw Error('Legacy directional transmission discarded')
      view.dispose()
      const blocked = emptyModelData()
      blocked.material = data.material
      const extra = new Float32Array(data.vertices)
      for (let i = 0; i < extra.length; i += 12) {
        extra[i + 2] = -1
        extra[i + 7] = 0
      }
      blocked.vertices = new Float32Array([...data.vertices, ...extra])
      view = await createPathView(device, blocked, '/generated/', { linearOutput: true })
      const lamp = copySceneLights([
        {
          id: 'blocked',
          type: 'point',
          position: [0, 0, -2],
          intensity: 0.06,
          castShadow: true,
          shadowSoftness: 0,
        },
      ])
      for (const integrator of ['path', 'bidirectional', 'photon-mapping'])
        if ((await render(integrator, lamp)) > 1e-8) throw Error('Real blocker ignored')
      view.dispose()
      await device.queue.onSubmittedWorkDone()
      device.destroy()
      if (errors.length) throw Error(errors.join('\n'))
      return records
    },
    '/@fs' + realpathSync('.'),
  )
  writeFileSync(
    'build/validation/caustic-audit/web-dielectric.json',
    JSON.stringify(result, null, 2),
  )
  console.log(
    'PASS WebGPU dielectric direct transmission, signed visibility bias, bright-light linearity and real blockers',
    result,
  )
} finally {
  await browser.close()
}
