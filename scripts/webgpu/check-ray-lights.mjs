import { chromium } from 'playwright'
import { realpathSync, mkdirSync, writeFileSync } from 'node:fs'
import { decode } from './Screenshot.mjs'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
mkdirSync('build/validation/ray-lights', { recursive: true })
try {
  const page = await browser.newPage({ viewport: { width: 800, height: 650 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(process.env.NOVA_TEST_URL || 'http://127.0.0.1:5220')
  const result = await page.evaluate(
    async (root) => {
      const { createPathView, defaultPathTracing } = await import(root + '/web/src/PathView.js'),
        { emptyModelData } = await import(root + '/web/src/ModelData.js'),
        { defaultEnvironment } = await import(root + '/web/src/EnvironmentData.js'),
        { copySceneLights } = await import(root + '/web/src/Lighting.js')
      const adapter = await navigator.gpu.requestAdapter(),
        device = await adapter.requestDevice({
          requiredLimits: { maxStorageBuffersPerShaderStage: 8 },
        })
      const errors = []
      device.addEventListener('uncapturederror', (e) => errors.push(e.error.message))
      const data = emptyModelData()
      data.vertices = new Float32Array([
        -1, -1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, -1, 0, 1, 0, 0, 1, 0, 1, 0,
        0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1,
        -1, -1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1,
        0, 0, 0, 0, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1,
      ])
      data.images[0].pixels = new Uint8Array([200, 200, 200, 255])
      data.material = [0, 0.8, 1, 1]
      const view = await createPathView(device, data, '/generated/', { linearOutput: true }),
        environment = { ...defaultEnvironment(), intensity: 0, showBackground: false },
        path = { ...defaultPathTracing(), maxBounces: 1, resolutionScale: 1 },
        camera = { yaw: 0, pitch: 0, distance: 3.6, exposure: 1 }
      const lights = copySceneLights(
        Array.from({ length: 6 }, (_, i) => ({
          id: String(i),
          type: i < 2 ? 'directional' : i < 4 ? 'point' : 'spot',
          position: [i % 2 ? 1 : -1, 0, 2],
          direction: i < 2 ? [i % 2 ? 1 : -1, 0, 2] : [i % 2 ? -0.5 : 0.5, 0, -1],
          color: [i % 3 === 0 ? 1 : 0, i % 3 === 1 ? 1 : 0, i % 3 === 2 ? 1 : 0],
          intensity: i < 2 ? 2 : 8,
          outerConeAngle: 1,
          shadowSoftness: 0,
        })),
      )
      const energy = async (list, settings = path) => {
        view.reset()
        view.draw(
          null,
          camera,
          [0, 0, 0, 1],
          48,
          48,
          'pbr',
          { direction: [0, 0, 1], color: [1, 1, 1], intensity: 100 },
          null,
          environment,
          settings,
          undefined,
          list,
          { enabled: true, bias: 0.002 },
        )
        const image = await view.readPixels(),
          values = new Float32Array(image.pixels.buffer)
        if (!values.every((v) => Number.isFinite(v) && v >= 0)) throw Error('Invalid radiance')
        return values.reduce((a, b) => a + b, 0) / values.length
      }
      if ((await energy([])) > 1e-8) throw Error('Empty ray collection retained legacy')
      let single = 0
      for (const light of lights) {
        const v = await energy([light])
        if (v < 0.001) throw Error('Missing ray light ' + light.id)
        single += v
      }
      const all = await energy(lights)
      if (Math.abs(all - single) > Math.max(1e-6, single * 1e-4))
        throw Error('Ray lights not additive')
      for (let i = 0; i < 6; i++)
        if ((await energy(lights.filter((_, k) => k !== i))) >= all - 0.001)
          throw Error('Missing light in sum')
      if ((await energy(lights.map((l) => ({ ...l, enabled: false })))) > 1e-8)
        throw Error('Disabled rays contribute')
      if (
        (await energy([{ ...lights[2], range: 0.1 }])) > 1e-8 ||
        (await energy([{ ...lights[4], direction: [0, 0, 1] }])) > 1e-8
      )
        throw Error('Range/cone ignored')
      const [rectangle] = copySceneLights([
        {
          id: 'area',
          up: [0, 1, 0],
          type: 'rectangle',
          position: [0, 0, 2],
          direction: [0, 0, -1],
          width: 2,
          height: 1,
        },
      ])
      const areaEnergy = await energy([rectangle])
      if (areaEnergy < 0.001) throw Error('Rectangle emitted no light')
      if ((await energy([{ ...rectangle, width: 0.1 }])) > 0.2 * areaEnergy)
        throw Error('Rectangle area ignored')
      if ((await energy([{ ...rectangle, direction: [0, 0, 1] }])) > 1e-8)
        throw Error('Rectangle emits through its back face')
      const active = copySceneLights(lights)
      await energy(active)
      view.draw(
        null,
        camera,
        [0, 0, 0, 1],
        48,
        48,
        'pbr',
        { direction: [0, 0, 1], color: [1, 1, 1], intensity: 100 },
        null,
        environment,
        path,
        undefined,
        active,
        { enabled: true, bias: 0.002 },
      )
      if (view.samples !== 2) throw Error('Accumulation stopped')
      active[0].intensity = 3
      view.draw(
        null,
        camera,
        [0, 0, 0, 1],
        48,
        48,
        'pbr',
        { direction: [0, 0, 1], color: [1, 1, 1], intensity: 100 },
        null,
        environment,
        path,
        undefined,
        active,
        { enabled: true, bias: 0.002 },
      )
      if (view.samples !== 1) throw Error('Light edit did not reset')
      for (const integrator of ['path', 'bidirectional', 'photon-mapping', 'mlt'])
        if (
          (await energy(lights, {
            ...path,
            integrator,
            mltBootstrapSamples: 256,
            photonsPerIteration: 256,
          })) < 0.001
        )
          throw Error('Integrator lost lights: ' + integrator)
      await device.queue.onSubmittedWorkDone()
      view.dispose()
      device.destroy()
      if (errors.length) throw Error(errors.join('\n'))
      return { single, all }
    },
    '/@fs' + realpathSync('.'),
  )
  // Exercise the public scene API with transformed instances, material slots and mode persistence.
  await page.evaluate(
    async (root) => {
      const { createRenderer } = await import(root + '/web/src/Nova.js')
      document.body.innerHTML = '<canvas style="width:480px;height:320px"></canvas>'
      window.rayErrors = []
      document
        .querySelector('canvas')
        .addEventListener('nova-error', (e) => rayErrors.push(String(e.detail)))
      window.renderer = await createRenderer({
        canvas: document.querySelector('canvas'),
        shaderDirectory: '/generated/',
      })
      window.assets = [
        { id: 'red', url: '/generated/SceneRed.glb' },
        { id: 'blue', url: '/generated/SceneBlue.glb' },
      ]
      const matrix = (x) => [0.4, 0, 0, 0, 0, 0.4, 0, 0, 0, 0, 0.4, 0, x, 0, 0, 1]
      window.instances = [
        { id: 'left', assetId: 'red', world: matrix(-0.7) },
        { id: 'right', assetId: 'blue', world: matrix(0.7) },
      ]
      await renderer.setScene(assets, instances)
      renderer.setViewCamera({
        position: [0, 0, 4],
        projection: 'orthographic',
        orthographicHeight: 2,
      })
      renderer.setEnvironment({ ...renderer.getEnvironment(), intensity: 0, showBackground: false })
      renderer.setClearColor([0, 0, 0, 1])
      renderer.setRayTracing({ ...renderer.getRayTracing(), maxBounces: 1, resolutionScale: 1 })
      renderer.setLights([{ id: 'sun', direction: [0, 0, 1], intensity: 4 }])
      renderer.setRenderMode('ray-tracing')
      await renderer.drawFrame()
      if (!renderer.getCapabilities().rayTracing || !renderer.getLightingCapabilities().point)
        throw Error('Scene ray capability missing')
    },
    '/@fs' + realpathSync('.'),
  )
  await page.locator('canvas').screenshot({ path: 'build/validation/ray-lights/web-scene.png' })
  await page.evaluate(async () => {
    instances[1].world[12] = 1.2
    await renderer.setScene(assets, instances)
    if (renderer.getRenderMode() !== 'ray-tracing') throw Error('Scene update lost ray mode')
    await renderer.drawFrame()
    const m = renderer.getMaterials('blue')[0]
    m.baseColor = [0, 1, 0, 1]
    await renderer.setMaterial(0, m, 'blue')
    await renderer.drawFrame()
  })
  await page
    .locator('canvas')
    .screenshot({ path: 'build/validation/ray-lights/web-scene-moved-material.png' })
  const displayed = decode(await page.locator('canvas').screenshot())
  let red = 0,
    green = 0
  for (let i = 0; i < displayed.pixels.length; i += displayed.channels) {
    const p = displayed.pixels
    if (p[i] > 80 && p[i] > p[i + 1] * 2) red++
    if (p[i + 1] > 80 && p[i + 1] > p[i] * 2) green++
  }
  if (red < 100 || green < 100)
    throw Error('Material update lost a displayed ray instance: ' + red + ',' + green)
  const asyncErrors = await page.evaluate(() => rayErrors)
  if (asyncErrors.length) throw Error(asyncErrors.join('\n'))
  await page.evaluate(async () => {
    await renderer.setScene([], [])
    await renderer.drawFrame()
    renderer.setRenderMode('raster')
    await renderer.drawFrame()
    renderer.dispose()
  })
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS WebGPU ray light additivity, accumulation, four integrators, transformed scenes and materials',
    result,
  )
} finally {
  await browser.close()
}
