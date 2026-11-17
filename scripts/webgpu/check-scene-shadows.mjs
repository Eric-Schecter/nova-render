import { chromium } from 'playwright'
import { realpathSync, mkdirSync, writeFileSync } from 'node:fs'
import { decode } from './Screenshot.mjs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5220'
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
  for (const type of ['point', 'spot']) {
    await page.evaluate(async (type) => {
      instances[1].world[12] = 0
      instances[1].world[13] = 0.3
      instances[1].castShadow = true
      await renderer.setScene(assets, instances)
      window.local = {
        id: type,
        type,
        position: [-1, 3, 1],
        direction: [1, -3, -1],
        intensity: 35,
        innerConeAngle: 0.4,
        outerConeAngle: 1,
        shadowSoftness: 0,
        castShadow: false,
      }
      renderer.setShadows({ enabled: true, samples: 16 })
      renderer.setLights([local])
      await renderer.drawFrame()
      const caps = renderer.getLightingCapabilities()
      if (!caps.pointShadows || !caps.spotShadows) throw Error('Missing local shadow capabilities')
    }, type)
    const unshadowed = await capture(type + '-off')
    await page.evaluate(async () => {
      local.castShadow = true
      renderer.setLights([local])
      await renderer.drawFrame()
    })
    const hard = await capture(type + '-hard')
    let darker = 0
    for (let i = 0; i < hard.pixels.length; i += hard.channels)
      if (
        unshadowed.pixels[i] + unshadowed.pixels[i + 1] + unshadowed.pixels[i + 2] >
        hard.pixels[i] + hard.pixels[i + 1] + hard.pixels[i + 2] + 12
      )
        darker++
    if (darker < 100) throw Error(type + ' shadow missing ' + darker)
    await page.evaluate(async () => {
      local.shadowSoftness = 4
      renderer.setLights([local])
      await renderer.drawFrame()
    })
    if ((await capture(type + '-soft')).pixels.equals(hard.pixels))
      throw Error(type + ' soft equals hard')
    await page.evaluate(async () => {
      renderer.setShadows({ enabled: false })
      await renderer.drawFrame()
    })
    if (!(await capture(type + '-global-off')).pixels.equals(unshadowed.pixels))
      throw Error(type + ' global switch failed')
    await page.evaluate(async () => {
      renderer.setShadows({ enabled: true })
      local.shadowSoftness = 0
      instances[1].castShadow = false
      await renderer.setScene(assets, instances)
      renderer.setLights([local])
      await renderer.drawFrame()
    })
    if (!(await capture(type + '-excluded')).pixels.equals(unshadowed.pixels))
      throw Error(type + ' excluded caster still shadows')
    await page.evaluate(async () => {
      instances[1].castShadow = true
      instances[1].world[12] = 1
      await renderer.setScene(assets, instances)
      await renderer.drawFrame()
    })
    if ((await capture(type + '-moved')).pixels.equals(hard.pixels))
      throw Error(type + ' movement ignored')
    await page.evaluate(async () => {
      local.position = [1, 2, 0]
      local.direction = [-1, -2, 0]
      local.innerConeAngle = 0.5
      local.outerConeAngle = 1.2
      instances[1].world[12] = 2
      instances[1].world[13] = 4
      await renderer.setScene(assets, instances)
      renderer.setLights([local])
      await renderer.drawFrame()
    })
    const finite = await capture(type + '-behind-source')
    await page.evaluate(async () => {
      instances[1].castShadow = false
      await renderer.setScene(assets, instances)
      await renderer.drawFrame()
    })
    if (!(await capture(type + '-behind-excluded')).pixels.equals(finite.pixels))
      throw Error(type + ' geometry behind source casts shadow')
    for (const outside of ['range', 'cone']) {
      if (outside === 'cone' && type !== 'spot') continue
      await page.evaluate(async (outside) => {
        local.range = outside === 'range' ? 0.1 : 0
        if (outside === 'cone') local.direction = [0, 1, 0]
        local.castShadow = false
        renderer.setLights([local])
        await renderer.drawFrame()
      }, outside)
      const off = await capture(type + '-' + outside + '-off')
      await page.evaluate(async () => {
        local.castShadow = true
        renderer.setLights([local])
        await renderer.drawFrame()
      })
      if (!(await capture(type + '-' + outside + '-on')).pixels.equals(off.pixels))
        throw Error(type + ' shadow affects outside ' + outside)
    }
  }
  await page.evaluate(async () => {
    instances[1].castShadow = true
    instances[1].world[12] = 0
    instances[1].world[13] = 0.3
    await renderer.setScene(assets, instances)
    window.point = {
      id: 'mixed-point',
      type: 'point',
      position: [-1, 3, 1],
      intensity: 25,
      color: [1, 0.3, 0.2],
      castShadow: true,
      shadowSoftness: 0,
    }
    window.spot = {
      ...point,
      id: 'mixed-spot',
      type: 'spot',
      position: [1, 3, 1],
      direction: [-1, -3, -1],
      outerConeAngle: 1,
      color: [0.2, 0.4, 1],
    }
    renderer.setLights([point, spot])
    await renderer.drawFrame()
  })
  const mixed = await capture('mixed-local-hard')
  for (const type of ['point', 'spot']) {
    await page.evaluate(async (type) => {
      renderer.setLights([
        { ...point, castShadow: type !== 'point' },
        { ...spot, castShadow: type !== 'spot' },
      ])
      await renderer.drawFrame()
    }, type)
    if ((await capture('mixed-' + type + '-off')).pixels.equals(mixed.pixels))
      throw Error(type + ' shadow missing in mixed collection')
  }
  await page.evaluate(() => renderer.dispose())
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS WebGPU directional/point/spot hard/soft shadows, finite segments, mixed flags, movement and validation; pixels',
    darker,
  )
} finally {
  await browser.close()
}
