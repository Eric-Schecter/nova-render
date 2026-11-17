import { chromium } from 'playwright'
import { mkdirSync, writeFileSync, readFileSync, existsSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
const dir = 'build/validation/pipelines'
mkdirSync(dir, { recursive: true })
for (const pass of [25, 26]) {
  const shader = JSON.parse(readFileSync(`docs/public/generated/aa-${pass}.json`, 'utf8'))
  if (!shader.compute?.includes('@compute') || shader.frag || shader.vert)
    throw Error(`Pass ${pass} must contain a compute kernel, without a raster fallback`)
}
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1100, height: 800 } })
  const errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(process.env.NOVA_TEST_URL || 'http://127.0.0.1:5220')
  const result = await page.evaluate(
    async (root) => {
      const { createPipelineComparisonView, rasterPipelines, pipelineDebugViews } = await import(
        root + '/web/src/Nova.js'
      )
      const canvas = document.createElement('canvas')
      document.body.replaceChildren(canvas)
      const view = await createPipelineComparisonView({ canvas })
      const half = (h) => {
        const e = (h >> 10) & 31,
          m = h & 1023
        return (h & 32768 ? -1 : 1) * (e === 0 ? m * 2 ** -24 : (m + 1024) * 2 ** (e - 25))
      }
      async function pixels() {
        const r = await view.readPixels(),
          d = new Uint16Array(r.pixels.buffer),
          out = []
        for (let y = 0; y < r.height; y++)
          for (let x = 0; x < r.width * 4; x++) {
            const v = half(d[(y * r.rowBytes) / 2 + x])
            if (!Number.isFinite(v)) throw Error('Nonfinite pipeline output')
            out.push(Math.round(Math.max(0, Math.min(1, v)) * 255))
          }
        return out
      }
      const difference = (a, b) => a.reduce((s, v, i) => s + Math.abs(v - b[i]), 0) / a.length
      const results = [],
        images = []
      for (const lightCount of [0, 128, 256]) {
        let reference
        for (const pipeline of rasterPipelines) {
          view.setSettings({ pipeline, lightCount, lightRadius: lightCount === 256 ? 32 : 3 })
          await view.drawFrame(321, 181)
          const image = await pixels()
          if (!reference) reference = image
          const error = difference(image, reference)
          if (error > 0.15) throw Error(`Image mismatch ${pipeline} ${lightCount}: ${error}`)
          results.push({ pipeline, lightCount, error })
          if (lightCount === 128) images.push(image)
        }
      }
      const debugImages = []
      for (const debug of pipelineDebugViews.slice(1)) {
        let reference
        for (const pipeline of rasterPipelines) {
          view.setSettings({ pipeline, debug, lightCount: 128, lightRadius: 3 })
          await view.drawFrame(321, 181)
          const image = await pixels()
          if (!reference) reference = image
          if (pipelineDebugViews.indexOf(debug) < 6 && difference(image, reference) > 0.15)
            throw Error(`Diagnostic mismatch ${pipeline} ${debug}`)
          if (pipeline === 'deferred') debugImages.push({ debug, pixels: image })
        }
      }
      view.setSettings({ pipeline: 'forward', debug: 'light-count' })
      await view.drawFrame(321, 181)
      const allLights = await pixels()
      for (const pipeline of ['forward-plus', 'clustered-forward']) {
        view.setSettings({ pipeline, debug: 'light-count' })
        await view.drawFrame(321, 181)
        if (difference(allLights, await pixels()) < 2)
          throw Error('Culling did not reduce candidate counts')
        view.setSettings({ debug: 'overflow', lightCount: 256, lightRadius: 32 })
        await view.drawFrame(321, 181)
        const image = await pixels()
        if (
          !image.some((v, i) => i % 4 === 0 && v > 240 && image[i + 1] < 10 && image[i + 2] > 240)
        )
          throw Error('Overflow fixture failed')
        view.setSettings({ lightCount: 128, lightRadius: 3 })
      }
      view.setSettings({ lightCount: 96, lightRadius: 2.5, debug: 'final' })
      view.setCamera({ yaw: -65, pitch: 38, distance: 14, target: [1, 1, 1] })
      for (const [width, height] of [
        [16, 9],
        [127, 65],
        [641, 359],
      ]) {
        view.setSettings({ pipeline: 'forward' })
        await view.drawFrame(width, height)
        const reference = await pixels()
        for (const pipeline of ['forward-plus', 'clustered-forward']) {
          view.setSettings({ pipeline })
          await view.drawFrame(width, height)
          if (difference(reference, await pixels()) > 0.15)
            throw Error(`Resized/camera compute parity failed: ${width}x${height} ${pipeline}`)
        }
      }
      view.dispose()
      return { results, images, debugImages }
    },
    '/@fs' + fileURLToPath(new URL('../../', import.meta.url)).replace(/\/$/, ''),
  )
  const nativeParity = []
  for (const api of ['metal', 'vulkan']) {
    if (!existsSync(`${dir}/${api}/0-0.ppm`)) continue
    for (let mode = 0; mode < 4; mode++) {
      const file = readFileSync(`${dir}/${api}/0-${mode}.ppm`)
      const offset = file.indexOf(Buffer.from('\n255\n')) + 5
      const rgb = file.subarray(offset),
        web = result.images[mode]
      let difference = 0
      for (let i = 0; i < rgb.length; i++)
        difference += Math.abs(rgb[i] - web[Math.floor(i / 3) * 4 + (i % 3)])
      difference /= rgb.length
      if (difference > 0.15) throw Error(`${api}/WebGPU mismatch: ${difference}`)
      nativeParity.push({ api, mode, difference })
    }
  }
  result.nativeParity = nativeParity
  if (errors.length) throw Error(errors.join('\n'))
  writeFileSync(`${dir}/web.json`, JSON.stringify(result))
  console.log(JSON.stringify(result.results, null, 2))
} finally {
  await browser.close()
}
