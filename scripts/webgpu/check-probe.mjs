import { chromium } from 'playwright'
import { readFileSync, mkdirSync, writeFileSync, realpathSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5205',
  native = JSON.parse(readFileSync('build/validation/probe-metal.json', 'utf8'))
mkdirSync('build/validation/probe', { recursive: true })
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
      const { createReflectionProbeView } = await import(root + '/web/src/Nova.js')
      const c = document.createElement('canvas')
      c.style = 'width:768px;height:576px'
      document.body.replaceChildren(c)
      const view = await createReflectionProbeView({ canvas: c })
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
            if (!Number.isFinite(v)) throw Error('Nonfinite probe pixel')
            out.push(Math.round(Math.max(0, Math.min(1, v)) * 255))
          }
        return out
      }
      const mae = (a, b) => a.reduce((s, v, i) => s + Math.abs(v - b[i]), 0) / a.length,
        images = [],
        stats = []
      for (let mode = 0; mode < 8; mode++) {
        view.setReflectionProbe({
          enabled: mode !== 0,
          boxProjection: mode !== 1,
          roughness: mode === 3 ? 0.8 : 0.12,
          intensity: mode === 4 ? 0 : 1,
          position: mode === 5 ? [1.3, 0.6, 1.5] : [0, 0.6, 0],
          debug: mode === 6 ? 'influence' : mode === 7 ? 'cubemap' : 'composite',
        })
        await view.drawFrame(256, 192)
        const image = await pixels(),
          error = mae(image, native[mode])
        if (error > 2) throw Error('Probe native/web parity ' + mode + ': ' + error)
        images.push(image)
        stats.push({ mode, nativeMAE: error })
      }
      for (const mode of [0, 1, 3, 4, 5])
        if (mae(images[2], images[mode]) < 0.1) throw Error('Missing probe/material effect ' + mode)
      view.setReflectionProbe({
        enabled: true,
        boxProjection: true,
        roughness: 0.12,
        intensity: 1,
        position: [0, 0.6, 0],
        debug: 'composite',
      })
      const captures = view.getProbeCaptureCount()
      for (const method of ['off', 'fxaa', 'smaa', 'msaa', 'ssaa', 'taa']) {
        view.setAntiAliasing({ method })
        await view.drawFrame(128, 96)
        await view.drawFrame(128, 96)
        if (method === 'taa' && view.getHistoryFrames() !== 2)
          throw Error('probe TAA history failed')
      }
      view.setTime(0.1)
      await view.drawFrame(128, 96)
      if (view.getHistoryFrames() !== 3) throw Error('Animated reflection lost history')
      view.setCamera({ yaw: view.getCamera().yaw + 2 })
      await view.drawFrame(128, 96)
      if (view.getHistoryFrames() !== 4) throw Error('Camera motion lost history')
      view.setCamera({ yaw: view.getCamera().yaw + 40 })
      if (view.getHistoryFrames() !== 0) throw Error('Camera cut kept history')
      if (view.getProbeCaptureCount() !== captures)
        throw Error('AA/camera unnecessarily recaptured probe')
      view.updateProbe()
      await view.drawFrame(128, 96)
      if (view.getProbeCaptureCount() !== captures + 1) throw Error('Manual refresh failed')
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
      view.setReflectionProbe({ enabled: true, debug: 'composite' })
      await view.drawFrame(256, 192)
      const old = view.getReflectionProbe()
      let rejected = false
      try {
        view.setReflectionProbe({ roughness: -1 })
      } catch {
        rejected = true
      }
      if (!rejected || view.getReflectionProbe().roughness !== old.roughness)
        throw Error('Invalid settings mutated state')
      view.setAntiAliasing({ method: 'off' })
      view.setPostProcessing({ autoExposure: false })
      view.setReflectionProbe({
        boxMin: [-0.2, 1.8, -0.2],
        boxMax: [0.2, 2.2, 0.2],
        position: [0, 2, 0],
      })
      await view.drawFrame(128, 96)
      const outside = await pixels()
      view.setReflectionProbe({ enabled: false })
      await view.drawFrame(128, 96)
      if (mae(outside, await pixels()) > 0.01)
        throw Error('Outside influence volume must use environment')
      const copied = view.getReflectionProbe()
      copied.position[0] = NaN
      if (!Number.isFinite(view.getReflectionProbe().position[0]))
        throw Error('Getter leaked settings array')
      for (const settings of [
        { position: [NaN, 0, 0] },
        { boxMin: [4, 4, 4] },
        { roughness: 2 },
        { debug: 'invalid' },
      ]) {
        let rejected = false
        try {
          view.setReflectionProbe(settings)
        } catch {
          rejected = true
        }
        if (!rejected) throw Error('Invalid probe settings accepted')
      }
      view.setReflectionProbe({
        enabled: true,
        boxMin: [-4, -1, -4],
        boxMax: [4, 3, 4],
        position: [0, 0.6, 0],
      })
      await view.drawFrame(256, 192)
      window.probeView = view
      return { stats, captures: view.getProbeCaptureCount() }
    },
    { root: '/@fs' + realpathSync('.'), native },
  )
  await p.locator('canvas').screenshot({ path: 'build/validation/probe/web.png' })
  await p.evaluate(() => window.probeView.dispose())
  if (errors.length) throw Error(errors.join('\n'))
  writeFileSync('build/validation/probe/web-parity.json', JSON.stringify(result, null, 2))
  console.log(
    'PASS local cube, box projection, native/web parity, six AA, post, NPOT/tiny sizes, real HDR environment and validation',
    result,
  )
} finally {
  await b.close()
}
