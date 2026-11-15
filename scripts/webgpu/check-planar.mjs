import { chromium } from 'playwright'
import { readFileSync, mkdirSync, writeFileSync, realpathSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5204',
  native = JSON.parse(readFileSync('build/validation/planar-metal.json', 'utf8'))
mkdirSync('build/validation/planar', { recursive: true })
const b = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const p = await b.newPage({ viewport: { width: 1100, height: 900 } }),
    errors = []
  p.on('pageerror', (e) => errors.push(e.message))
  await p.goto(base)
  const result = await p.evaluate(
    async ({ root, native }) => {
      const { createPlanarReflectionView } = await import(root + '/web/src/Nova.js')
      const c = document.createElement('canvas')
      c.style = 'width:768px;height:576px'
      document.body.replaceChildren(c)
      const view = await createPlanarReflectionView({ canvas: c })
      view.setAntiAliasing({ method: 'off' })
      view.setPostProcessing({ bloom: false })
      const half = (h) => {
        const s = h & 32768 ? -1 : 1,
          e = (h >> 10) & 31,
          m = h & 1023
        return e === 0 ? s * m * 2 ** -24 : s * (m + 1024) * 2 ** (e - 25)
      }
      async function pixels() {
        const r = await view.readPixels(),
          data = new Uint16Array(r.pixels.buffer),
          out = []
        for (let y = 0; y < r.height; y++)
          for (let x = 0; x < r.width * 4; x++) {
            const v = half(data[(y * r.rowBytes) / 2 + x])
            if (!Number.isFinite(v)) throw Error('Nonfinite planar pixel')
            out.push(Math.round(Math.max(0, Math.min(1, v)) * 255))
          }
        return out
      }
      const mae = (a, b) => a.reduce((s, v, i) => s + Math.abs(v - b[i]), 0) / a.length,
        images = [],
        stats = []
      for (let mode = 0; mode < 6; mode++) {
        const settings = {
          floorEnabled: mode !== 0 && mode !== 3,
          mirrorEnabled: mode !== 0 && mode !== 2,
          floorRoughness: mode === 4 ? 0.8 : 0.06,
          mirrorRoughness: mode === 4 ? 0.8 : 0.02,
          floorIntensity: mode === 5 ? 0 : 1,
          mirrorIntensity: mode === 5 ? 0 : 1,
        }
        view.setPlanarReflections(settings)
        await view.drawFrame(256, 192)
        const image = await pixels(),
          error = mae(image, native[mode])
        if (error > 2) throw Error('Planar native/web parity ' + mode + ': ' + error)
        images.push(image)
        stats.push({ mode, nativeMAE: error })
      }
      for (const mode of [0, 2, 3, 4, 5])
        if (mae(images[1], images[mode]) < 0.1)
          throw Error('Missing planar/material effect ' + mode)
      const magenta = (image) =>
        image.reduce(
          (n, v, i) =>
            n +
            (i % 4 === 0 &&
            v > 80 &&
            v > image[i + 1] * 1.5 &&
            image[i + 2] > image[i + 1] * 1.3 &&
            image[i + 2] > 50
              ? 1
              : 0),
          0,
        )
      if (magenta(images[1]) < 5 || magenta(images[0]) > 0)
        throw Error('Offscreen magenta witness missing from mirror')
      view.setPlanarReflections({
        floorEnabled: true,
        mirrorEnabled: true,
        floorRoughness: 0.06,
        mirrorRoughness: 0.02,
        floorIntensity: 1,
        mirrorIntensity: 1,
      })
      for (const method of ['off', 'fxaa', 'smaa', 'msaa', 'ssaa', 'taa']) {
        view.setAntiAliasing({ method })
        await view.drawFrame(128, 96)
        await view.drawFrame(128, 96)
        if (method === 'taa' && view.getHistoryFrames() !== 2)
          throw Error('planar TAA history failed')
      }
      view.setTime(0.1)
      await view.drawFrame(128, 96)
      if (view.getHistoryFrames() !== 3) throw Error('Animated reflection lost history')
      view.setCamera({ yaw: view.getCamera().yaw + 2 })
      await view.drawFrame(128, 96)
      if (view.getHistoryFrames() !== 4) throw Error('Camera motion lost history')
      view.setCamera({ yaw: view.getCamera().yaw + 40 })
      if (view.getHistoryFrames() !== 0) throw Error('Camera cut kept history')
      view.setAntiAliasing({ method: 'ssaa' })
      view.setPostProcessing({ autoExposure: true, bloom: true })
      await view.drawFrame(137, 93)
      await view.drawFrame(1, 1)
      await view.loadEnvironment('/generated/venice_sunset.bin')
      await view.drawFrame(128, 96)
      const envA = await pixels()
      await view.loadEnvironment('/generated/studio_small_09.bin')
      await view.drawFrame(128, 96)
      if (mae(envA, await pixels()) < 0.1) throw Error('Environment fallback not changing')
      view.useDefaultEnvironment()
      view.setPlanarReflections({ floorEnabled: true, mirrorEnabled: true })
      await view.drawFrame(256, 192)
      const old = view.getPlanarReflections()
      let rejected = false
      try {
        view.setPlanarReflections({ floorRoughness: -1 })
      } catch {
        rejected = true
      }
      if (!rejected || view.getPlanarReflections().floorRoughness !== old.floorRoughness)
        throw Error('Invalid settings mutated state')
      window.planarView = view
      return { stats, offscreenWitnessPixels: magenta(images[1]) }
    },
    { root: '/@fs' + realpathSync('.'), native },
  )
  await p.locator('canvas').screenshot({ path: 'build/validation/planar/web.png' })
  await p.evaluate(() => window.planarView.dispose())
  if (errors.length) throw Error(errors.join('\n'))
  writeFileSync('build/validation/planar/web-parity.json', JSON.stringify(result, null, 2))
  console.log(
    'PASS planar mirrors, native/web parity, six AA, post, NPOT/tiny sizes, real HDR environment and validation',
    result,
  )
} finally {
  await b.close()
}
