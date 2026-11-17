import { chromium } from 'playwright'
import { realpathSync, mkdirSync, writeFileSync } from 'node:fs'
import { decode } from './Screenshot.mjs'
import assert from 'node:assert/strict'
import { copySceneLights } from '../../web/src/Lighting.js'
for (const invalid of [
  [{ id: 'a', type: 'spot', outerConeAngle: 2 }],
  [{ id: 'a', type: 'point', shadowSoftness: -1 }],
  [{ id: 'a' }, { id: 'a' }],
  Array.from({ length: 17 }, (_, i) => ({ id: String(i) })),
])
  assert.throws(() => copySceneLights(invalid))
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
mkdirSync('build/validation/lights', { recursive: true })
try {
  const page = await browser.newPage({ viewport: { width: 700, height: 550 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(process.env.NOVA_TEST_URL || 'http://127.0.0.1:5220')
  await page.evaluate(
    async (root) => {
      const { createRenderer } = await import(root + '/web/src/Nova.js')
      document.body.innerHTML = '<canvas style="width:480px;height:360px"></canvas>'
      window.renderer = await createRenderer({
        canvas: document.querySelector('canvas'),
        shaderDirectory: '/generated/',
      })
      await renderer.loadModel('/generated/MaterialDual.glb')
      for (let i = 0; i < 2; i++) {
        const m = renderer.getMaterials()[i]
        m.baseColor = [0.6, 0.6, 0.6, 1]
        m.metallic = 0
        m.roughness = 0.8
        await renderer.setMaterial(i, m)
      }
      renderer.setViewCamera({
        position: [0, 0, 4],
        projection: 'orthographic',
        orthographicHeight: 2.6,
      })
      renderer.setLighting('pbr')
      renderer.setAntiAliasing({ method: 'fxaa' })
      window.lights = [
        { id: 'd1', direction: [-1, 1, 2], color: [1, 0, 0], intensity: 2 },
        { id: 'd2', direction: [1, 1, 2], color: [0, 1, 0], intensity: 2 },
        { id: 'p1', type: 'point', position: [-1, 0, 1], color: [0, 0, 1], intensity: 4 },
        { id: 'p2', type: 'point', position: [1, 0, 1], color: [1, 0, 0], intensity: 4 },
        {
          id: 's1',
          type: 'spot',
          position: [-1, 0, 2],
          direction: [0.3, 0, -1],
          color: [0, 1, 0],
          intensity: 8,
          outerConeAngle: 0.7,
        },
        {
          id: 's2',
          type: 'spot',
          position: [1, 0, 2],
          direction: [-0.3, 0, -1],
          color: [0, 0, 1],
          intensity: 8,
          outerConeAngle: 0.7,
        },
      ]
      if (renderer.getLightingCapabilities().maxLights !== 16) throw Error('Capabilities')
      renderer.setLights([])
      await renderer.drawFrame()
    },
    '/@fs' + realpathSync('.'),
  )
  const capture = async (name) => {
    const png = await page.locator('canvas').screenshot()
    writeFileSync('build/validation/lights/web-' + name + '.png', png)
    return decode(png)
  }
  const delta = (a, b) => {
    let sum = 0
    for (let i = 0; i < a.pixels.length; i++) sum += Math.abs(a.pixels[i] - b.pixels[i])
    return sum / a.pixels.length
  }
  const empty = await capture('empty')
  const images = []
  for (let i = 0; i < 6; i++) {
    await page.evaluate(async (i) => {
      renderer.setLights([lights[i]])
      await renderer.drawFrame()
    }, i)
    const im = await capture('single-' + i)
    images.push(im)
    assert(delta(empty, im) > 0.15, `Light ${i} did not contribute: ${delta(empty, im)}`)
  }
  await page.evaluate(async () => {
    renderer.setLights(lights)
    await renderer.drawFrame()
  })
  const all = await capture('all')
  for (let i = 0; i < 6; i++) {
    await page.evaluate(async (i) => {
      renderer.setLights(lights.filter((_, n) => n !== i))
      await renderer.drawFrame()
    }, i)
    assert(delta(all, await capture('without-' + i)) > 0.05, `Light ${i} missing from sum`)
  }
  await page.evaluate(async () => {
    renderer.setLights(lights)
    const before = JSON.stringify(renderer.getLights())
    for (const bad of [
      [...lights, { id: 'd1' }],
      [{ id: 'bad', type: 'point', range: -1 }],
      [{ id: 'bad', type: 'spot', outerConeAngle: 2 }],
    ]) {
      let rejected = false
      try {
        renderer.setLights(bad)
      } catch {
        rejected = true
      }
      if (!rejected || JSON.stringify(renderer.getLights()) !== before)
        throw Error('Nontransactional validation')
    }
    const copy = renderer.getLights()
    copy[0].color[0] = 0
    if (renderer.getLights()[0].color[0] !== 1) throw Error('Leaking mutable lights')
    let rejected = false
    try {
      renderer.setRenderMode('ray-tracing')
    } catch {
      rejected = true
    }
    if (!rejected || renderer.getRenderMode() !== 'raster') throw Error('Unsupported mode accepted')
    renderer.setLights(lights.map((l) => ({ ...l, enabled: false })))
    await renderer.drawFrame()
  })
  assert(delta(empty, await capture('disabled')) < 0.01, 'Disabled collection added hidden light')
  for (const [name, change, index] of [
    ['range', { range: 0.1 }, 2],
    ['away', { direction: [0, 0, 1] }, 4],
    ['far', { position: [-1, 0, 100] }, 2],
    ['cone', { innerConeAngle: 0, outerConeAngle: 0.01 }, 4],
  ]) {
    await page.evaluate(
      async ({ change, index }) => {
        renderer.setLights([{ ...lights[index], ...change }])
        await renderer.drawFrame()
      },
      { change, index },
    )
    const changed = await capture(name)
    assert(delta(images[index], changed) > 0.1, `${name} did not affect light`)
  }
  await page.evaluate(async () => {
    renderer.setDirectionalLight({ direction: [0, 0, 1], color: [1, 1, 1], intensity: 2 })
    if (renderer.getLights().length !== 1) throw Error('Legacy replacement')
    await renderer.drawFrame()
  })
  assert(delta(empty, await capture('legacy')) > 0.1)
  assert.deepEqual(errors, [])
  console.log(
    'WebGPU lights: six simultaneous contributions, range/cones, validation, empty/disabled, legacy and mode capabilities passed',
  )
} finally {
  await browser.close()
}
