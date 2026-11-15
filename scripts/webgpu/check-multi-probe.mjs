import { chromium } from 'playwright'
import { readFileSync, realpathSync, mkdirSync, writeFileSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5206',
  native = JSON.parse(readFileSync('build/validation/multi-probe-metal.json', 'utf8'))
mkdirSync('build/validation/multi-probe', { recursive: true })
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage(),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(base)
  const result = await page.evaluate(
    async ({ root, native }) => {
      const { createReflectionProbeView } = await import(root + '/web/src/Nova.js'),
        { roomProbes } = await import(root + '/web/examples/probe.js')
      const canvas = document.createElement('canvas')
      canvas.style = 'width:768px;height:576px'
      document.body.replaceChildren(canvas)
      const view = await createReflectionProbeView({ canvas })
      view.setAntiAliasing({ method: 'off' })
      view.setPostProcessing({ bloom: false })
      let settings = { ...view.getReflectionProbe(), probes: roomProbes() }
      view.setReflectionProbe(settings)
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
        settings.debug =
          mode < 3 ? 'composite' : mode < 6 ? 'influence' : mode === 6 ? 'reflections' : 'cubemap'
        settings.debugProbe = mode === 7 ? 2 : 0
        settings.smoothBlending = mode !== 6
        view.setReflectionProbe(settings)
        view.setTime(mode % 3 === 0 ? 4.712389 : mode % 3 === 1 ? 0 : 1.5707963)
        await view.drawFrame(256, 192)
        const image = await pixels(),
          error = mae(image, native[mode])
        if (error > 2) throw Error('Multi-probe native/web parity ' + mode + ': ' + error)
        images.push(image)
        stats.push({ mode, nativeMAE: error })
      }
      if (view.getProbeCaptureCount() !== 3) throw Error('Animation recaptured probes')
      if (mae(images[0], images[2]) < 0.5) throw Error('Object did not cross probe regions')
      settings.debug = 'composite'
      settings.debugProbe = 0
      settings.smoothBlending = true
      settings.probes[1].position[1] = 0.8
      view.setReflectionProbe(settings)
      await view.drawFrame(128, 96)
      if (view.getProbeCaptureCount() !== 4)
        throw Error('Moving one probe recaptured unrelated probes')
      settings.probes[0].intensity = 0.6
      settings.probes[1].blendDistance = 0.5
      view.setReflectionProbe(settings)
      await view.drawFrame(128, 96)
      if (view.getProbeCaptureCount() !== 4) throw Error('Blending changes recaptured probes')
      view.updateProbe(2)
      await view.drawFrame(128, 96)
      if (view.getProbeCaptureCount() !== 5) throw Error('Indexed refresh failed')
      for (const method of ['off', 'fxaa', 'smaa', 'msaa', 'ssaa', 'taa']) {
        view.setAntiAliasing({ method })
        await view.drawFrame(137, 93)
        await view.drawFrame(137, 93)
      }
      if (view.getProbeCaptureCount() !== 5) throw Error('AA/resize lost cube cache')
      settings.probes.push({ ...settings.probes[0], position: [1, 0.6, 1] })
      view.setReflectionProbe(settings)
      await view.drawFrame(137, 93)
      if (view.getProbeCaptureCount() !== 6) throw Error('Fourth probe capture failed')
      let rejected = false
      try {
        view.setReflectionProbes([...settings.probes, settings.probes[0]])
      } catch {
        rejected = true
      }
      if (!rejected || view.getReflectionProbes().length !== 4)
        throw Error('Invalid count mutated state')
      settings.probes = Array.from({ length: 3 }, () => ({
        enabled: true,
        intensity: 1,
        blendDistance: 0,
        position: [0, 0.6, 0],
        boxMin: [-4, -1, -4],
        boxMax: [4, 3, 4],
        influenceMin: [-4, -1, -4],
        influenceMax: [4, 3, 4],
      }))
      view.setAntiAliasing({ method: 'off' })
      view.setTime(0)
      view.setReflectionProbe(settings)
      await view.drawFrame(128, 96)
      const all = await pixels()
      settings.probes[1].enabled = settings.probes[2].enabled = false
      view.setReflectionProbe(settings)
      await view.drawFrame(128, 96)
      if (mae(all, await pixels()) > 0.02)
        throw Error('Overlapping probes multiplied reflected energy')
      for (const probe of settings.probes) {
        probe.influenceMin = [10, 10, 10]
        probe.influenceMax = [11, 11, 11]
      }
      view.setReflectionProbe(settings)
      await view.drawFrame(128, 96)
      const outside = await pixels()
      view.setReflectionProbe({ enabled: false })
      await view.drawFrame(128, 96)
      if (mae(outside, await pixels()) > 0.01) throw Error('No outside-region fallback')
      view.setPostProcessing({ autoExposure: true, bloom: true })
      await view.drawFrame(1, 1)
      await view.drawFrame(137, 93)
      const copied = view.getReflectionProbes()
      copied[0].position[0] = NaN
      if (!Number.isFinite(view.getReflectionProbes()[0].position[0]))
        throw Error('Getter leaked probe arrays')
      view.dispose()
      return stats
    },
    { root: '/@fs' + realpathSync('.'), native },
  )
  if (errors.length) throw Error(errors.join('\n'))
  writeFileSync('build/validation/multi-probe/web-parity.json', JSON.stringify(result, null, 2))
  console.log(
    'PASS multi-probe native/web parity, crossing, normalized blend, independent cache, fourth probe, six AA and fallback',
    result,
  )
} finally {
  await browser.close()
}
