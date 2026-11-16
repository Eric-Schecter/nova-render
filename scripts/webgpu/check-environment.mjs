import { chromium } from 'playwright'
import { resolve } from 'node:path'
import { readFile } from 'node:fs/promises'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 1000 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto('http://127.0.0.1:5173/examples/environment')
  await page.locator('[data-ready="true"]').waitFor({ timeout: 45000 })
  const canvas = page.locator('canvas')
  async function frame() {
    const bytes = await canvas.screenshot()
    return page.evaluate(async (encoded) => {
      const bytes = Uint8Array.from(atob(encoded), (c) => c.charCodeAt(0)),
        image = await createImageBitmap(new Blob([bytes], { type: 'image/png' }))
      const c = new OffscreenCanvas(image.width, image.height),
        ctx = c.getContext('2d')
      ctx.drawImage(image, 0, 0)
      image.close()
      const pixels = ctx.getImageData(0, 0, c.width, c.height).data
      const sample = (x0, y0, x1, y1) => {
        const result = []
        for (let y = y0; y < y1; y += 3)
          for (let x = x0; x < x1; x += 3) {
            const index = (Math.floor(y) * c.width + Math.floor(x)) * 4
            result.push(...pixels.slice(index, index + 3))
          }
        return result.join(',')
      }
      return {
        sky: sample(10, 10, 50, 50),
        model: sample(c.width * 0.44, c.height * 0.35, c.width * 0.56, c.height * 0.52),
      }
    }, bytes.toString('base64'))
  }
  async function changed(previous, field) {
    for (let i = 0; i < 12; i++) {
      const next = await frame()
      if (next[field] !== previous[field]) return next
    }
    throw Error('No GPU change in ' + field)
  }
  const initial = await frame()
  await page.getByRole('slider', { name: 'Environment intensity', exact: true }).fill('0')
  const dark = await changed(initial, 'model')
  if (dark.sky !== initial.sky) throw Error('IBL strength changed sky brightness')
  await page.getByRole('slider', { name: 'Skybox intensity', exact: true }).fill('0.2')
  const dim = await changed(dark, 'sky')
  if (dim.model !== dark.model) throw Error('Sky brightness changed IBL lighting')
  await page.getByRole('slider', { name: 'Environment rotation', exact: true }).fill('90')
  const rotated = await changed(dim, 'sky')
  await page.getByRole('slider', { name: 'Environment intensity', exact: true }).fill('1')
  const lit = await changed(rotated, 'model')
  if (lit.sky !== rotated.sky) throw Error('Lighting leaked into sky')
  await page.getByRole('checkbox', { name: 'Show skybox', exact: true }).uncheck()
  const hidden = await changed(lit, 'sky')
  if (hidden.model !== lit.model) throw Error('Hiding sky disabled model IBL')
  const background = page.getByRole('combobox', { name: 'Clear color', exact: true })
  if (!(await background.isEnabled())) throw Error('Clear color remains disabled without sky')
  await page.getByRole('button', { name: 'Reset environment', exact: true }).click()
  const reset = await changed(hidden, 'sky')
  if (reset.sky !== initial.sky || reset.model !== initial.model)
    throw Error('Environment reset did not restore GPU output')
  if (!(await background.isDisabled())) throw Error('Clear color should be disabled behind sky')
  for (const [tab, file] of [
    ['C++ example', 'examples/environment/main.cpp'],
    ['JavaScript example', 'web/examples/environment.js'],
    ['HLSL shader', 'render/shaders/model.hlsl'],
  ]) {
    await page.getByRole('tab', { name: tab, exact: true }).click()
    if ((await page.getByRole('textbox').inputValue()) !== (await readFile(file, 'utf8')))
      throw Error('Wrong environment example source')
  }
  await page.getByRole('button', { name: 'Collapse parameters', exact: true }).click()
  await page.locator('.nova-viewport').screenshot({ path: 'build/webgpu/environment-final.png' })
  await page.evaluate(
    async (moduleURL) => {
      const Nova = await import(moduleURL),
        canvas = document.createElement('canvas')
      canvas.id = 'environment-only'
      canvas.style = 'display:block;width:400px;height:300px'
      document.body.append(canvas)
      const r = await Nova.createRenderer({ canvas })
      window.environmentTestRenderer = r
      await r.loadEnvironment('/generated/venice_sunset.bin')
      await r.drawFrame()
      const cfg = r.getEnvironment()
      cfg.rotation = 90
      if (r.getEnvironment().rotation !== 0) throw Error('Environment getter is not a copy')
      for (const bad of [
        { ...cfg, intensity: -1 },
        { ...cfg, rotation: 181 },
        { ...cfg, backgroundIntensity: NaN },
        { ...cfg, showBackground: 1 },
      ]) {
        let rejected = false
        try {
          r.setEnvironment(bad)
        } catch {
          rejected = true
        }
        if (!rejected) throw Error('Invalid settings accepted')
      }
    },
    '/@fs/' + resolve('web/src/Nova.js'),
  )
  const only = page.locator('#environment-only'),
    before = await only.screenshot()
  await page.evaluate(async () => {
    environmentTestRenderer.setCamera({ distance: 10 })
    await environmentTestRenderer.drawFrame()
  })
  if (!before.equals(await only.screenshot())) throw Error('Skybox moved with camera translation')
  await page.evaluate(async () => {
    const r = environmentTestRenderer
    let rejected = false
    try {
      await r.loadEnvironment('/generated/missing.nenv')
    } catch {
      rejected = true
    }
    if (!rejected) throw Error('Missing environment accepted')
    const bad = URL.createObjectURL(new Blob(['NOVENV01bad']))
    try {
      rejected = false
      try {
        await r.loadEnvironment(bad)
      } catch {
        rejected = true
      }
      if (!rejected) throw Error('Truncated environment accepted')
    } finally {
      URL.revokeObjectURL(bad)
    }
    await r.drawFrame()
  })
  if (!before.equals(await only.screenshot()))
    throw Error('Failed environment load destroyed previous sky')
  await page.evaluate(async () => {
    const r = environmentTestRenderer
    r.setLighting('pbr')
    await r.loadModel('/generated/DamagedHelmet.glb')
    await r.drawFrame()
    // Serialized resource changes and a concurrent redraw must remain valid.
    await Promise.all([r.loadEnvironment('/generated/venice_sunset.bin'), r.drawFrame()])
    r.dispose()
    document.querySelector('#environment-only').remove()
  })
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS HDR sky/IBL pixels, independent sky and lighting controls, rotation, reset, actual source tabs, sky-only, translation invariance, failed-load rollback and resource serialization',
  )
} finally {
  await browser.close()
}
