import { chromium } from 'playwright'
import { realpathSync, mkdirSync, writeFileSync } from 'node:fs'
import { decode } from './Screenshot.mjs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5222'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
mkdirSync('build/validation/editor-shadows', { recursive: true })
try {
  const page = await browser.newPage({ viewport: { width: 700, height: 550 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(base)
  await page.evaluate(
    async (root) => {
      const { createRenderer } = await import(root + '/web/src/Nova.js')
      document.body.innerHTML = '<canvas style="width:480px;height:360px"></canvas>'
      const renderer = await createRenderer({
        canvas: document.querySelector('canvas'),
        shaderDirectory: '/generated/',
      })
      window.renderer = renderer
      const matrix = (x, y, z, tx = 0, ty = 0, tz = 0) => [
        x,
        0,
        0,
        0,
        0,
        y,
        0,
        0,
        0,
        0,
        z,
        0,
        tx,
        ty,
        tz,
        1,
      ]
      window.assets = [{ id: 'box', url: '/generated/SceneRed.glb' }]
      window.instances = [
        { id: 'ground', assetId: 'box', world: matrix(2, 0.05, 2, 0, -0.8) },
        { id: 'caster', assetId: 'box', world: matrix(0.3, 1, 0.3, 0, 0.3) },
      ]
      await renderer.setScene(assets, instances)
      const white = renderer.getMaterials('box')[0]
      white.baseColor = [1, 1, 1, 1]
      white.metallic = 0
      white.roughness = 0.7
      await renderer.setMaterial(0, white, 'box')
      renderer.setCamera({ yaw: 30, pitch: 25, distance: 6, exposure: 1, target: [0, 0, 0] })
      renderer.setDirectionalLight({
        direction: [-1, 2, 1],
        intensity: 8,
        color: [1, 3.2 / 3.5, 0.8],
      })
      renderer.setLighting('pbr')
      renderer.setAntiAliasing({ method: 'fxaa' })
      await renderer.drawFrame()
    },
    '/@fs' + realpathSync('.'),
  )
  const capture = async (name) => {
    const png = await page.locator('canvas').screenshot()
    writeFileSync(`build/validation/editor-shadows/web-${name}.png`, png)
    return decode(png)
  }
  const off = await capture('off')
  await page.evaluate(async () => {
    renderer.setShadows({ enabled: true, softness: 0 })
    await renderer.drawFrame()
  })
  const hard = await capture('hard')
  let darker = 0
  for (let i = 0; i < off.pixels.length; i += off.channels)
    if (
      off.pixels[i] + off.pixels[i + 1] + off.pixels[i + 2] >
      hard.pixels[i] + hard.pixels[i + 1] + hard.pixels[i + 2] + 12
    )
      darker++
  if (darker < 200) throw Error('GPU shadow missing ' + darker)
  await page.evaluate(async () => {
    renderer.setShadows({ softness: 4, samples: 16 })
    await renderer.drawFrame()
  })
  const soft = await capture('soft')
  if (soft.pixels.equals(hard.pixels)) throw Error('Soft shadow equals hard')
  await page.evaluate(async () => {
    renderer.setShadows({ enabled: false })
    await renderer.drawFrame()
  })
  if (!(await capture('disabled')).pixels.equals(off.pixels))
    throw Error('Disabled shadows did not restore image')
  await page.evaluate(async () => {
    instances[1].castShadow = false
    await renderer.setScene(assets, instances)
    renderer.setShadows({ enabled: true, softness: 0 })
    await renderer.drawFrame()
  })
  if (!(await capture('excluded')).pixels.equals(off.pixels))
    throw Error('Disabled caster still casts')
  await page.evaluate(async () => {
    instances[1].castShadow = true
    instances[1].world[12] = 1
    await renderer.setScene(assets, instances)
    await renderer.drawFrame()
    let rejected = false
    try {
      renderer.setShadows({ samples: 0 })
    } catch {
      rejected = true
    }
    if (!rejected || renderer.getShadows().samples !== 16) throw Error('Shadow validation failed')
  })
  if ((await capture('moved')).pixels.equals(hard.pixels))
    throw Error('Caster movement retained stale shadow')
  await page.evaluate(() => renderer.dispose())
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS WebGPU scene hard/soft shadows, caster flags, movement and validation; pixels',
    darker,
  )
} finally {
  await browser.close()
}
