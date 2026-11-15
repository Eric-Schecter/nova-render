import { chromium } from 'playwright'
import { readFileSync, mkdirSync, writeFileSync, realpathSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5203',
  native = JSON.parse(readFileSync('build/validation/ssr-metal.json', 'utf8'))
mkdirSync('build/validation/ssr', { recursive: true })
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
      const { createScreenSpaceReflectionView } = await import(root + '/web/src/Nova.js')
      const c = document.createElement('canvas')
      c.style = 'width:768px;height:576px'
      document.body.replaceChildren(c)
      const view = await createScreenSpaceReflectionView({ canvas: c })
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
            if (!Number.isFinite(v)) throw Error('Nonfinite SSR pixel')
            out.push(Math.round(Math.max(0, Math.min(1, v)) * 255))
          }
        return out
      }
      const mae = (a, b) => a.reduce((s, v, i) => s + Math.abs(v - b[i]), 0) / a.length,
        images = [],
        stats = []
      for (let mode = 0; mode < 7; mode++) {
        const debug =
          mode < 2
            ? 'composite'
            : [
                'composite',
                'reflections',
                'hit-mask',
                'normals',
                'roughness',
                'linear-depth',
                'depth-pyramid',
              ][mode]
        view.setScreenSpaceReflections({ enabled: mode !== 0, debug })
        await view.drawFrame(256, 192)
        const image = await pixels(),
          error = mae(image, native[mode])
        if (error > 2) throw Error('SSR native/web parity ' + mode + ': ' + error)
        images.push(image)
        stats.push({ mode, nativeMAE: error })
      }
      if (mae(images[0], images[1]) < 0.1) throw Error('SSR has no visible effect')
      let hits = 0
      for (let i = 0; i < images[2].length; i += 4) if (images[2][i] > 50) hits++
      if (hits < 30) throw Error('No SSR hits')
      view.setScreenSpaceReflections({ debug: 'composite', hierarchical: false })
      await view.drawFrame(256, 192)
      view.setScreenSpaceReflections({ hierarchical: true })
      for (const method of ['off', 'fxaa', 'smaa', 'msaa', 'ssaa', 'taa']) {
        view.setAntiAliasing({ method })
        await view.drawFrame(128, 96)
        await view.drawFrame(128, 96)
        if (method === 'taa' && view.getHistoryFrames() !== 2) throw Error('SSR TAA history failed')
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
      view.setScreenSpaceReflections({ debug: 'depth-pyramid', debugMip: 12 })
      await view.drawFrame(1, 1)
      await view.drawFrame(137, 93)
      view.setScreenSpaceReflections({ debug: 'reflections', enabled: false })
      await view.loadEnvironment('/generated/venice_sunset.bin')
      await view.drawFrame(128, 96)
      const envA = await pixels()
      await view.loadEnvironment('/generated/studio_small_09.bin')
      await view.drawFrame(128, 96)
      if (mae(envA, await pixels()) < 0.1) throw Error('Environment fallback not changing')
      view.useDefaultEnvironment()
      view.setScreenSpaceReflections({ debug: 'composite', enabled: true })
      await view.drawFrame(256, 192)
      const old = view.getScreenSpaceReflections()
      let rejected = false
      try {
        view.setScreenSpaceReflections({ thickness: -1 })
      } catch {
        rejected = true
      }
      if (!rejected || view.getScreenSpaceReflections().thickness !== old.thickness)
        throw Error('Invalid settings mutated state')
      window.ssrView = view
      return { stats, hits }
    },
    { root: '/@fs' + realpathSync('.'), native },
  )
  await p.locator('canvas').screenshot({ path: 'build/validation/ssr/web.png' })
  await p.evaluate(() => window.ssrView.dispose())
  if (errors.length) throw Error(errors.join('\n'))
  writeFileSync('build/validation/ssr/web-parity.json', JSON.stringify(result, null, 2))
  console.log(
    'PASS SSR hierarchy, hit mask, native/web parity, six AA, post, NPOT/tiny sizes, real HDR environment fallback and validation',
    result,
  )
} finally {
  await b.close()
}
