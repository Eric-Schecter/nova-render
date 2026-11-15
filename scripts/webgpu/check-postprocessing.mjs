import { chromium } from 'playwright'
import { readFileSync, writeFileSync, mkdirSync, realpathSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5202',
  native = JSON.parse(readFileSync('build/validation/post-metal.json', 'utf8'))
mkdirSync('build/validation/post', { recursive: true })
const b = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const p = await b.newPage({ viewport: { width: 1200, height: 900 } }),
    errors = []
  p.on('pageerror', (e) => errors.push(e.message))
  await p.goto(base)
  const results = await p.evaluate(
    async ({ root, native }) => {
      const { createPostProcessingView, createRenderer } = await import(root + '/web/src/Nova.js')
      const c = document.createElement('canvas')
      c.style = 'width:768px;height:512px'
      document.body.replaceChildren(c)
      const view = await createPostProcessingView({ canvas: c })
      view.setAntiAliasing({ method: 'off' })
      const half = (h) => {
        const s = h & 32768 ? -1 : 1,
          e = (h >> 10) & 31,
          m = h & 1023
        return e === 0 ? s * m * 2 ** -24 : s * (m + 1024) * 2 ** (e - 25)
      }
      async function pixels() {
        const r = await view.readPixels(),
          d = new Uint16Array(r.pixels.buffer),
          out = []
        for (let y = 0; y < r.height; y++)
          for (let x = 0; x < r.width * 4; x++) {
            const v = half(d[(y * r.rowBytes) / 2 + x])
            if (!Number.isFinite(v)) throw Error('Nonfinite HDR output')
            out.push(Math.round(Math.max(0, Math.min(1, v)) * 255))
          }
        return out
      }
      const diff = (a, b) => a.reduce((s, v, i) => s + Math.abs(v - b[i]), 0) / a.length,
        results = [],
        images = []
      for (let i = 0; i < 5; i++) {
        view.setPostProcessing({
          toneMapping: ['linear', 'reinhard', 'aces'][Math.min(i, 2)],
          bloom: i === 3,
          saturation: i === 4 ? 0 : 1,
          autoExposure: false,
        })
        await view.drawFrame(192, 128)
        const image = await pixels(),
          mae = diff(image, native[i])
        if (mae > 2) throw Error('HDR parity failed ' + i + ': ' + mae)
        results.push({ case: i, nativeMAE: mae })
        images.push(image)
      }
      if (diff(images[2], images[3]) < 0.1) throw Error('Bloom missing')
      view.setPostProcessing({ toneMapping: 'linear', exposureEV: -4, bloom: false, saturation: 1 })
      await view.drawFrame(192, 128)
      const hdr = await pixels()
      if (Math.max(...hdr.filter((_, i) => i % 4 !== 3)) < 200)
        throw Error('HDR clipped before exposure')
      view.setPostProcessing({ toneMapping: 'aces', exposureEV: 0 })
      view.setPostProcessing({ saturation: 1, bloom: true, autoExposure: true })
      await view.drawFrame(192, 128)
      view.setSceneIntensity(8)
      await view.drawFrame(192, 128)
      const bright = await pixels()
      for (let i = 0; i < 16; i++) await view.drawFrame(192, 128)
      if (diff(bright, await pixels()) < 0.01) throw Error('Exposure not adapting')
      for (const method of ['off', 'fxaa', 'smaa', 'msaa', 'ssaa', 'taa']) {
        view.setAntiAliasing({ method })
        await view.drawFrame(128, 96)
        await view.drawFrame(128, 96)
        if (method === 'taa' && view.getHistoryFrames() !== 2) throw Error('TAA failed')
      }
      await view.drawFrame(137, 93)
      await view.drawFrame(1, 1)
      const old = view.getPostProcessing()
      let rejected = false
      try {
        view.setPostProcessing({ bloomIntensity: -1 })
      } catch {
        rejected = true
      }
      if (!rejected || view.getPostProcessing().bloomIntensity !== old.bloomIntensity)
        throw Error('Invalid settings mutated state')
      view.dispose()
      // Public renderer, ordinary triangle and material/sky path reuse the same HDR post chain.
      c.style = 'width:192px;height:128px'
      const renderer = await createRenderer({ canvas: c })
      renderer.setContinuousRendering(false)
      await renderer.setShader(await (await fetch('/generated/triangle.json')).json())
      renderer.setPostProcessing({ enabled: true, autoExposure: true })
      await renderer.drawFrame()
      await renderer.loadModel('/generated/DamagedHelmet.glb')
      await renderer.loadEnvironment('/generated/venice_sunset.bin')
      renderer.setLighting('pbr')
      for (const method of ['off', 'fxaa', 'smaa', 'msaa', 'ssaa', 'taa']) {
        renderer.setAntiAliasing({ method })
        await renderer.drawFrame()
      }
      renderer.setPostProcessing({ enabled: false })
      await renderer.drawFrame()
      renderer.dispose()
      return results
    },
    { root: '/@fs' + realpathSync('.'), native },
  )
  if (errors.length) throw Error(errors.join('\n'))
  writeFileSync('build/validation/post/web-parity.json', JSON.stringify(results, null, 2))
  console.log(
    'PASS HDR native/web parity, curves, bloom, grading, exposure adaptation, AA, resize, validation and ordinary renderer post chain',
    results,
  )
} finally {
  await b.close()
}
