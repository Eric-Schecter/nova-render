import { chromium } from 'playwright'
import { readFileSync, mkdirSync, writeFileSync, realpathSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5198'
const native = JSON.parse(readFileSync('build/validation/aa-metal.json', 'utf8'))
mkdirSync('build/validation/aa', { recursive: true })
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1000, height: 800 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(base)
  const result = await page.evaluate(
    async ({ root, native }) => {
      const { createAntiAliasingView } = await import(root + '/web/src/AntiAliasingView.js')
      const c = document.createElement('canvas')
      document.body.replaceChildren(c)
      c.style = 'width:768px;height:576px;image-rendering:pixelated'
      const v = await createAntiAliasingView({ canvas: c }),
        methods = ['off', 'fxaa', 'smaa', 'msaa', 'ssaa', 'taa'],
        images = [],
        stats = []
      const half = (h) => {
        const s = h & 32768 ? -1 : 1,
          e = (h >> 10) & 31,
          m = h & 1023
        return e === 0 ? s * m * 2 ** -24 : e === 31 ? NaN : s * (m + 1024) * 2 ** (e - 25)
      }
      async function pixels() {
        const r = await v.readPixels(),
          data = new Uint16Array(r.pixels.buffer),
          bytes = []
        for (let y = 0; y < r.height; y++)
          for (let x = 0; x < r.width * 4; x++) {
            const f = half(data[(y * r.rowBytes) / 2 + x])
            if (!Number.isFinite(f)) throw Error('Non-finite pixel')
            bytes.push(Math.round(Math.max(0, Math.min(1, f)) * 255))
          }
        return bytes
      }
      const error = (a, b) => a.reduce((s, v, i) => s + Math.abs(v - b[i]), 0) / a.length
      for (let i = 0; i < methods.length; i++) {
        v.setAntiAliasing({ method: methods[i], split: false })
        if (v.getHistoryFrames() !== 0) throw Error('History mode reset failed')
        for (let f = 0; f < (i === 5 ? 8 : 1); f++) await v.drawFrame(128, 96)
        const values = await pixels(),
          mae = error(values, native[i])
        images.push(values)
        if (mae > 2) throw Error('Native/web pixel parity failed ' + methods[i] + ' ' + mae)
        if (i > 0 && error(values, images[0]) < 0.1) throw Error('AA did not change image')
        stats.push({
          method: methods[i],
          nativeMAE: mae,
          change: error(values, images[0]),
          referenceMAE: 0,
        })
      }
      stats.forEach((s, i) => (s.referenceMAE = error(images[i], images[4])))
      if (stats[5].referenceMAE >= stats[0].referenceMAE * 0.9)
        throw Error('TAA failed static convergence against SSAA reference')
      // Accumulation must respond to changing geometry, not freeze or blindly blend history.
      v.setTime(0.1)
      await v.drawFrame(128, 96)
      const moving = await pixels()
      if (error(moving, images[5]) < 0.01) throw Error('Moving scene did not update')
      v.setTime(3)
      if (v.getHistoryFrames() !== 0) throw Error('Time cut history retained')
      const camera = v.getCamera()
      v.setCamera({ ...camera, yaw: camera.yaw + 40 })
      if (v.getHistoryFrames() !== 0) throw Error('Camera cut history retained')
      await v.drawFrame(128, 96)
      await v.drawFrame(64, 48)
      if (v.getHistoryFrames() !== 1) throw Error('Resize did not reset')
      let rejected = false
      try {
        v.setAntiAliasing({ historyWeight: 1 })
      } catch {
        rejected = true
      }
      if (!rejected) throw Error('Invalid history settings accepted')
      v.setCamera(camera)
      v.setTime(0)
      v.setAntiAliasing({ method: 'taa', zoom: 4, split: true })
      for (let i = 0; i < 8; i++) await v.drawFrame(192, 144)
      window.aaView = v
      return stats
    },
    { root: '/@fs' + realpathSync('.'), native },
  )
  await page.locator('canvas').screenshot({ path: 'build/validation/aa/web-zoom.png' })
  await page.evaluate(() => window.aaView.dispose())
  if (errors.length) throw Error(errors.join('\n'))
  writeFileSync('build/validation/aa/web-parity.json', JSON.stringify(result, null, 2))
  console.log(
    'PASS six AA methods, native parity, TAA motion/cuts/resize, invalid settings and magnified split',
    result,
  )
} finally {
  await browser.close()
}
