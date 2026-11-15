import { chromium } from 'playwright'
import { readFileSync, writeFileSync, mkdirSync, realpathSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5201'
const native = JSON.parse(readFileSync('build/validation/shadow-metal.json', 'utf8'))
mkdirSync('build/validation/shadows', { recursive: true })
const b = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const p = await b.newPage({ viewport: { width: 1440, height: 1100 } }),
    errors = []
  p.on('pageerror', (e) => errors.push(e.message))
  await p.goto(base)
  const result = await p.evaluate(
    async ({ root, native }) => {
      const { createShadowView } = await import(root + '/web/src/Nova.js')
      const canvas = document.createElement('canvas')
      canvas.style = 'width:768px;height:576px'
      document.body.replaceChildren(canvas)
      const view = await createShadowView({ canvas })
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
            const value = half(d[(y * r.rowBytes) / 2 + x])
            if (!Number.isFinite(value)) throw Error('Nonfinite output')
            out.push(Math.round(Math.max(0, Math.min(1, value)) * 255))
          }
        return out
      }
      const mae = (a, b) => a.reduce((s, v, i) => s + Math.abs(v - b[i]), 0) / a.length,
        images = [],
        stats = []
      for (const [i, method] of ['off', 'hard', 'pcf', 'pcss'].entries()) {
        view.setShadowSettings({ method })
        await view.drawFrame(256, 192)
        const image = await pixels(),
          error = mae(image, native[i])
        if (error > 2) throw Error(method + ' native/web parity ' + error)
        if (i && mae(image, images[0]) < 0.1) throw Error('Missing shadow ' + method)
        images.push(image)
        stats.push({ method, nativeMAE: error })
      }
      for (const method of ['off', 'fxaa', 'smaa', 'msaa', 'ssaa', 'taa']) {
        view.setAntiAliasing({ method })
        await view.drawFrame(128, 96)
        await view.drawFrame(128, 96)
        if (method === 'taa' && view.getHistoryFrames() !== 2)
          throw Error('History did not accumulate')
      }
      view.setCamera({ yaw: 30 })
      await view.drawFrame(128, 96)
      if (view.getHistoryFrames() !== 3) throw Error('Small camera reset')
      view.setCamera({ yaw: 80 })
      if (view.getHistoryFrames() !== 0) throw Error('Cut history retained')
      for (const resolution of [512, 2048]) {
        view.setShadowSettings({ resolution })
        await view.drawFrame(128, 96)
      }
      const before = view.getShadowSettings()
      let rejected = false
      try {
        view.setShadowSettings({ resolution: 123 })
      } catch {
        rejected = true
      }
      if (!rejected || view.getShadowSettings().resolution !== before.resolution)
        throw Error('Invalid settings changed view')
      view.dispose()
      return stats
    },
    { root: '/@fs' + realpathSync('.'), native },
  )
  await p.goto(base + '/examples/shadows')
  await p.locator('[data-ready="true"]').waitFor({ timeout: 120000 })
  const panel = p.getByRole('region', { name: '阴影参数', exact: true }),
    canvas = p.locator('canvas'),
    bar = p.getByLabel('渲染性能')
  await canvas.scrollIntoViewIfNeeded()
  const a = await bar.boundingBox(),
    c = await canvas.boundingBox(),
    u = await panel.boundingBox()
  if (a.y + a.height > c.y + 1 || u.y < c.y + c.height - 1) throw Error('UI overlaps image')
  await p.getByRole('combobox', { name: '抗锯齿算法', exact: true }).selectOption('off')
  for (const method of ['off', 'hard', 'pcf', 'pcss']) {
    await p.getByRole('combobox', { name: '阴影算法', exact: true }).selectOption(method)
    await p.waitForTimeout(250)
    await canvas.screenshot({ path: `build/validation/shadows/web-${method}.png` })
  }
  await p.getByLabel('光源尺寸', { exact: true }).fill('1')
  await p.getByLabel('光源尺寸', { exact: true }).dispatchEvent('input')
  await p.getByRole('combobox', { name: '抗锯齿算法', exact: true }).selectOption('taa')
  await p.waitForFunction(
    () => document.querySelector('[aria-label="渲染性能"]').textContent.includes('32 accumulated'),
    null,
    { timeout: 30000 },
  )
  await p.getByRole('button', { name: '隐藏预览', exact: true }).click()
  await p.getByRole('button', { name: '显示预览', exact: true }).click()
  await p.setViewportSize({ width: 390, height: 900 })
  await canvas.scrollIntoViewIfNeeded()
  await p.waitForTimeout(500)
  if (await p.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1))
    throw Error('Mobile overflow')
  await p.screenshot({ path: 'build/validation/shadows/mobile.png' })
  await p.goto(base + '/examples/triangle')
  if (errors.length) throw Error(errors.join('\n'))
  writeFileSync('build/validation/shadows/web-parity.json', JSON.stringify(result, null, 2))
  console.log(
    'PASS shadows: four modes, native pixel parity, six AA, map resize, history/cuts, invalid settings, UI/layout/mobile/hide/disposal',
    result,
  )
} finally {
  await b.close()
}
