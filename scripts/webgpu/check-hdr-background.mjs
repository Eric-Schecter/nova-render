import { chromium } from 'playwright'
import { readFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { createHash } from 'node:crypto'
const bytes = await readFile('assets/Environment/venice_sunset.nenv')
const iblBytes = 4194288
const legacy = Buffer.concat([
  Buffer.from('NOVENV01'),
  bytes.subarray(8, 160),
  bytes.subarray(168, 168 + iblBytes),
])
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 1100 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.route('**/legacy-environment.nenv', (route) =>
    route.fulfill({ body: legacy, contentType: 'application/octet-stream' }),
  )
  await page.goto('http://127.0.0.1:5173/examples/environment')
  await page.locator('[data-ready="true"]').waitFor({ timeout: 45000 })
  await page.evaluate(
    async ({ novaURL, dataURL }) => {
      const Nova = await import(novaURL),
        loader = await import(dataURL)
      const environment = await loader.loadEnvironmentFile('/generated/venice_sunset.bin')
      if (
        environment.mips[0].size !== 256 ||
        environment.backgroundMips[0].size !== 512 ||
        environment.backgroundMips.length !== 10
      )
        throw Error('Background was not separated from IBL')
      const old = await loader.loadEnvironmentFile('/legacy-environment.nenv')
      if (old.backgroundMips !== old.mips) throw Error('Legacy fallback unavailable')
      const malformed = new Uint8Array(
        await (await fetch('/generated/venice_sunset.bin')).arrayBuffer(),
      )
      new DataView(malformed.buffer).setUint32(160, 3, true)
      const blob = URL.createObjectURL(new Blob([malformed]))
      let rejected = false
      try {
        await loader.loadEnvironmentFile(blob)
      } catch {
        rejected = true
      } finally {
        URL.revokeObjectURL(blob)
      }
      if (!rejected) throw Error('Invalid background dimensions accepted')
      const host = document.createElement('div'),
        canvas = document.createElement('canvas')
      host.style = 'position:fixed;inset:0;z-index:9999;background:#111'
      canvas.id = 'hdr-test-canvas'
      canvas.style = 'width:1000px;height:750px'
      host.append(canvas)
      document.body.append(host)
      const renderer = await Nova.createRenderer({ canvas })
      await renderer.loadModel('/generated/DamagedHelmet.glb')
      renderer.setLighting('pbr')
      renderer.setDirectionalLight({ direction: [-1, 1.6, 2], color: [1, 1, 1], intensity: 0 })
      renderer.setCamera({ yaw: 20, pitch: 5 })
      window.hdrTest = { renderer, canvas, host }
    },
    {
      novaURL: '/@fs' + resolve('web/src/Nova.js'),
      dataURL: '/@fs' + resolve('web/src/EnvironmentData.js'),
    },
  )
  const canvas = page.locator('#hdr-test-canvas'),
    settings = { rotation: 0, intensity: 1, backgroundIntensity: 1, showBackground: false }
  async function draw(url, background) {
    await page.evaluate(
      async ({ url, settings, background }) => {
        const r = window.hdrTest.renderer
        await r.loadEnvironment(url)
        r.setEnvironment({ ...settings, showBackground: background })
        await r.drawFrame()
      },
      { url, settings, background },
    )
  }
  await draw('/legacy-environment.nenv', false)
  const before = createHash('sha256')
    .update(await canvas.screenshot())
    .digest('hex')
  await draw('/generated/venice_sunset.bin', false)
  const after = createHash('sha256')
    .update(await canvas.screenshot())
    .digest('hex')
  if (before !== after) throw Error('Independent background changed model lighting')
  await draw('/legacy-environment.nenv', true)
  await canvas.screenshot({ path: 'build/webgpu/hdr-background-before.png' })
  await draw('/generated/venice_sunset.bin', true)
  await canvas.screenshot({ path: 'build/webgpu/hdr-background-after.png' })
  await page.evaluate(async () => {
    window.hdrTest.canvas.style = 'width:200px;height:150px'
    await window.hdrTest.renderer.drawFrame()
  })
  await canvas.screenshot({ path: 'build/webgpu/hdr-background-small.png' })
  await page.evaluate(() => {
    window.hdrTest.renderer.dispose()
    window.hdrTest.host.remove()
  })
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS independent 512 HDR sky, spatial mip chain, unchanged model pixels, legacy asset fallback, bad-header rejection, minification and GPU cleanup',
  )
} finally {
  await browser.close()
}
