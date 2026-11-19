import assert from 'node:assert/strict'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { chromium } from 'playwright'
import { compile } from './compiler.mjs'
import { decode } from './Screenshot.mjs'

const [archive, baseline] = process.argv.slice(2)
if (!archive || !baseline)
  throw Error('Usage: check-hybrid-showroom.mjs showroom.zip baseline-model.hlsl')
const output = resolve('build/validation/hybrid-showroom')
await mkdir(output, { recursive: true })
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
const results = []
try {
  for (const [name, path] of [
    ['before', baseline],
    ['after', 'render/shaders/model.hlsl'],
  ]) {
    const shaders = {}
    const source = '#define NOVA_RASTER_AA 1\n' + (await readFile(path, 'utf8'))
    for (const [shader, fragmentEntry] of [
      ['model-hybrid-shadow', 'HybridShadowPSMain'],
      ['model-deferred', 'DeferredPSMain'],
    ]) {
      shaders[shader + '.json'] = await compile(source, {
        model: true,
        vertexEntry: 'SkyVSMain',
        fragmentEntry,
        optimizeModel: true,
        maxSourceBytes: 262144,
      })
    }
    const page = await browser.newPage({
      viewport: { width: 1600, height: 1000 },
      deviceScaleFactor: 1,
    })
    const errors = []
    page.on('pageerror', (e) => errors.push(e.message))
    await page.route('**/sdk/generated/model-*.json', (route) => {
      const shader = shaders[new URL(route.request().url()).pathname.split('/').at(-1)]
      return shader ? route.fulfill({ json: shader }) : route.continue()
    })
    if (name === 'after' && process.argv.includes('--instance-bounds'))
      await page.route('**/sdk/src/SceneData.js', (route) =>
        route.fulfill({ path: resolve('web/src/SceneData.js'), contentType: 'text/javascript' }),
      )
    await page.route('**/sdk/src/Nova.js', async (route) => {
      const body =
        (await (await route.fetch()).text()).replace(
          'export async function createRenderer(',
          'async function createRendererOriginal(',
        ) +
        `
export async function createRenderer(options) {
  const raw = await createRendererOriginal(options);
  window.showroom = {raw, paused: false};
  return new Proxy(raw, {get(target, key) {
    if (key === 'drawFrame') return () => showroom.paused ? Promise.resolve('skipped') : raw.drawFrame();
    const value = Reflect.get(target, key, target);
    return typeof value === 'function' ? value.bind(target) : value;
  }});
}
`
      await route.fulfill({ contentType: 'text/javascript', body })
    })
    await page.goto(process.env.NOVA_EDITOR_URL || 'http://127.0.0.1:5237/')
    await page.locator('input[accept=".zip"]').setInputFiles(resolve(archive))
    await page.getByText('5 个模型实例', { exact: true }).waitFor({ timeout: 60000 })
    await page.getByRole('button', { name: '视图', exact: true }).click()
    await page.getByRole('menuitem', { name: '预览主相机', exact: true }).click()
    await page.evaluate(async () => {
      showroom.raw.setContinuousRendering(false)
      await showroom.raw.waitIdle()
      showroom.paused = true
    })
    await page.getByRole('combobox', { name: '渲染模式' }).selectOption('hybrid')
    await page.evaluate(() => {
      const { raw } = showroom
      raw.setHybrid({
        shadowSamples: 1,
        aoSamples: 1,
        reflections: true,
        ambientOcclusion: true,
        shadows: true,
        temporalReconstruction: true,
      })
      const camera = raw.getViewCamera()
      showroom.measure = async (moving) => {
        raw.resetHybridHistory()
        for (let i = 0; i < 4; i++) await raw.drawFrame()
        const times = []
        for (let i = 0; i < 16; i++) {
          if (moving)
            raw.setViewCamera({
              position: camera.position.map((x, k) => x + (k === 0 ? 0.014 * i : 0)),
            })
          const start = performance.now()
          if ((await raw.drawFrame()) !== 'rendered') throw Error('Frame did not render')
          times.push(performance.now() - start)
        }
        times.sort((a, b) => a - b)
        return (times[7] + times[8]) / 2
      }
    })
    const timing = { name }
    const captures = {}
    for (const mode of ['still', 'moving']) {
      timing[mode] = await page.evaluate((moving) => showroom.measure(moving), mode === 'moving')
      captures[mode] = decode(
        await page
          .locator('canvas')
          .screenshot({ path: resolve(output, name + '-' + mode + '.png') }),
      )
    }
    await page.evaluate(async () => {
      const menu = document.querySelector('select[aria-label="渲染模式"]')
      if (menu?.value !== 'hybrid') throw Error('Hybrid mode was lost')
    })
    assert.deepEqual(errors, [])
    results.push({ timing, captures })
    await page.close()
    console.log(name, timing)
  }
  const [before, after] = results
  const mae = {}
  for (const mode of ['still', 'moving']) {
    const a = before.captures[mode],
      b = after.captures[mode]
    assert.equal(a.width, b.width)
    assert.equal(a.height, b.height)
    assert.equal(a.channels, b.channels)
    mae[mode] = a.pixels.reduce((sum, x, i) => sum + Math.abs(x - b.pixels[i]), 0) / a.pixels.length
    assert.ok(mae[mode] <= 0.15, mode + ' image MAE exceeded 0.15/255: ' + mae[mode])
  }
  await writeFile(
    resolve(output, 'web-report.json'),
    JSON.stringify({ mae, cases: results.map(({ timing }) => timing) }, null, 2),
  )
  console.log('Showroom image MAE', mae)
} finally {
  await browser.close()
}
