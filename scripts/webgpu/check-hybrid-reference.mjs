import { chromium } from 'playwright'
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { decode } from './Screenshot.mjs'
const probeComparison = process.argv.includes('--probe-comparison')
const bounceBaseline = probeComparison || process.argv.includes('--bounce-baseline')
const effects = !bounceBaseline && process.argv.includes('--effects')
const fixture = JSON.parse(
    readFileSync(
      'build/reference/hybrid-rendering/' + (effects ? 'scenes-effects.json' : 'scenes.json'),
    ),
  ),
  root = bounceBaseline
    ? 'build/validation/hybrid-gi-baseline'
    : effects
      ? 'build/validation/hybrid-reference-effects'
      : 'build/validation/hybrid-reference',
  dir = root + '/web'
mkdirSync(dir, { recursive: true })
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
const difference = (a, b) => {
  if (a.width !== b.width || a.height !== b.height) throw Error('Image size')
  let n = 0
  for (let i = 0; i < a.width * a.height; i++)
    for (let c = 0; c < 3; c++)
      n += Math.abs(a.pixels[i * a.channels + c] - b.pixels[i * b.channels + c])
  return n / (a.width * a.height * 3)
}
try {
  const page = await browser.newPage(),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(process.env.NOVA_TEST_URL || 'http://127.0.0.1:5220')
  await page.evaluate(async (f) => {
    const { createRenderer } = await import(
      '/@fs/Users/eric/Documents/repos/renderer/nova-v2/web/src/Nova.js'
    )
    const canvas = document.createElement('canvas')
    canvas.width = f.width
    canvas.height = f.height
    document.body.replaceChildren(canvas)
    window.r = await createRenderer({ canvas })
    r.setContinuousRendering(false)
    r.setPostProcessing({ enabled: true, bloom: false, autoExposure: false, toneMapping: 'aces' })
    r.setLighting('pbr')
    r.setDirectionalLight(f.light)
    r.setShadows({ enabled: true, softness: 0, samples: 1 })
    r.setHybrid({ shadowSamples: 1 })
  }, fixture)
  const results = []
  for (const scene of fixture.scenes) {
    if (bounceBaseline && scene.id !== 'global-illumination-test') continue
    await page.evaluate(async (s) => {
      r.setRenderMode('raster')
      await r.setScene(
        s.assets.map(({ id, url, sourceSpace }) => ({ id, url, sourceSpace })),
        s.instances,
      )
      r.setRasterSettings({ pipeline: 'deferred', view: 'final' })
    }, scene)
    for (const camera of scene.cameras) {
      await page.evaluate((c) => {
        const { id, ...value } = c
        r.setViewCamera(value)
      }, camera)
      if (bounceBaseline) {
        const result = { camera: camera.id, native: {} }
        const images = []
        const modes = ['off', 'pixel-bounce']
        if (probeComparison) modes.push('probes')
        for (const label of modes) {
          result[label] = await page.evaluate(async (mode) => {
            r.setRenderMode('hybrid')
            r.setHybrid({ diffuseBounce: mode === 'pixel-bounce', probeGI: mode === 'probes' })
            for (let n = 0; n < (mode === 'probes' ? 52 : 4); n++) {
              await r.drawFrame()
              await r.waitIdle()
            }
            const samples = []
            for (let n = 0; n < 15; n++) {
              const start = performance.now()
              await r.drawFrame()
              await r.waitIdle()
              samples.push(performance.now() - start)
            }
            return samples.sort((a, b) => a - b)[7]
          }, label)
          const name = scene.id + '-' + camera.id + '-' + label + '.png'
          const captured = decode(
            await page.locator('canvas').screenshot({ path: dir + '/' + name }),
          )
          images.push(captured)
          for (const api of ['metal', 'vulkan']) {
            const error = difference(captured, decode(readFileSync(root + '/' + api + '/' + name)))
            if (error > 1) throw Error(name + ' ' + api + ' MAE ' + error)
            result.native[api + '/' + label] = error
          }
        }
        result.effectMAE = difference(images[0], images[1])
        if (probeComparison) {
          result.probeEffectMAE = difference(images[0], images[2])
          result.probeToPixelMAE = difference(images[1], images[2])
          if (result.probeEffectMAE <= 0.001) throw Error('Probe mode did not change the image')
        }
        if (result.effectMAE <= 0.001) throw Error('GI room bounce did not change the image')
        results.push(result)
        console.log(result)
        continue
      }
      if (effects)
        await page.evaluate(() =>
          r.setHybrid({
            view: 'final',
            ambientOcclusion: false,
            reflections: false,
            temporalReconstruction: false,
            historyWeight: 0.9,
          }),
        )
      const name = scene.id + '-' + camera.id,
        captures = {}
      for (const mode of ['raster', 'hybrid']) {
        await page.evaluate(async (mode) => {
          r.setRenderMode(mode)
          await r.drawFrame()
        }, mode)
        const png = await page.locator('canvas').screenshot({ path: `${dir}/${name}-${mode}.png` })
        captures[mode] = decode(png)
      }
      const parity = difference(captures.raster, captures.hybrid)
      if (parity > 1) throw Error(name + ' Raster/Hybrid ' + parity)
      const native = {}
      for (const api of ['metal', 'vulkan']) {
        native[api] = difference(
          captures.hybrid,
          decode(readFileSync(`${root}/${api}/${name}-hybrid.png`)),
        )
        if (native[api] > 1) throw Error(name + ' ' + api + ' MAE ' + native[api])
      }
      const metrics = {}
      if (effects) {
        const h = fixture.hybridEffects,
          cases = {}
        const capture = async (label, settings, draw = true) => {
          await page.evaluate(
            async ({ settings, draw }) => {
              if (settings) r.setHybrid(settings)
              if (draw) await r.drawFrame()
            },
            { settings, draw },
          )
          const result = decode(
            await page.locator('canvas').screenshot({ path: `${dir}/${name}-${label}.png` }),
          )
          cases[label] = result
          return result
        }
        const ao = await capture('ao', {
          ambientOcclusion: true,
          aoRadius: h.aoRadius,
          aoSamples: h.aoSamples,
        })
        await capture('ao-visibility', { view: 'ambient-occlusion' })
        const reflection = await capture('reflections', {
          view: 'final',
          ambientOcclusion: false,
          reflections: true,
          reflectionSamples: h.reflectionSamples,
        })
        await capture('reflection-visibility', { view: 'reflections' })
        await capture('combined', { view: 'final', ambientOcclusion: true })
        const first = await capture('temporal-first', { temporalReconstruction: true })
        await page.evaluate(async (n) => {
          for (let f = 1; f < n; ++f) await r.drawFrame()
        }, h.historyFrames)
        const resolved = await capture('temporal-resolved', null, false)
        if ((await page.evaluate(() => r.getHybridHistoryFrames())) !== h.historyFrames)
          throw Error('Reference temporal history ' + name)
        metrics.aoDifference = difference(captures.hybrid, ao)
        metrics.reflectionDifference = difference(captures.hybrid, reflection)
        if (camera.id === 'angle-1') {
          await page.evaluate(() => r.setHybrid({ historyWeight: 0 }))
          const reference = new Float64Array(first.width * first.height * 3)
          for (let f = 0; f < h.historyFrames; ++f) {
            await page.evaluate(() => r.drawFrame())
            const raw = decode(await page.locator('canvas').screenshot())
            for (let i = 0; i < raw.width * raw.height; ++i)
              for (let c = 0; c < 3; ++c)
                reference[i * 3 + c] += raw.pixels[i * raw.channels + c] / h.historyFrames
          }
          const mse = (image) => {
            let sum = 0
            for (let i = 0; i < image.width * image.height; ++i)
              for (let c = 0; c < 3; ++c)
                sum += (image.pixels[i * image.channels + c] - reference[i * 3 + c]) ** 2
            return sum / (image.width * image.height * 4)
          }
          metrics.firstMSE = mse(first)
          metrics.resolvedMSE = mse(resolved)
          if (metrics.firstMSE > 0.01 && metrics.resolvedMSE >= metrics.firstMSE)
            throw Error('Reference temporal error did not improve ' + name)
        }
        metrics.native = {}
        for (const api of ['metal', 'vulkan'])
          for (const [label, image] of Object.entries(cases)) {
            const mae = difference(
              image,
              decode(readFileSync(`${root}/${api}/${name}-${label}.png`)),
            )
            if (mae > 1) throw Error(`${name}/${label}/${api} MAE ${mae}`)
            metrics.native[`${api}/${label}`] = mae
          }
      }
      results.push({
        scene: scene.id,
        camera: camera.id,
        parity,
        native,
        ...(effects ? { effects: metrics } : {}),
      })
      console.log(results.at(-1))
    }
  }
  if (effects)
    for (const scene of fixture.scenes) {
      const views = results.filter((r) => r.scene === scene.id)
      if (
        !views.some((r) => r.effects.aoDifference > 0.001) ||
        !views.some((r) => r.effects.reflectionDifference > 0.001)
      )
        throw Error('Missing reference effect: ' + scene.id)
    }
  if (errors.length) throw Error(errors.join('\n'))
  writeFileSync(
    root + (probeComparison ? '/web-probe-metrics.json' : '/web-metrics.json'),
    JSON.stringify(results, null, 2),
  )
  console.log(
    (bounceBaseline ? 'Four GI room cameras: ' : 'Original three scenes / twelve fixed cameras: ') +
      (bounceBaseline
        ? 'GI room baseline and native parity passed'
        : effects
          ? 'AO/reflections/temporal effects and native parity passed'
          : 'WebGPU and native parity passed'),
  )
} finally {
  await browser.close()
}
