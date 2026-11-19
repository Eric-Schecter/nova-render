import { chromium } from 'playwright'
import { mkdirSync, readFileSync } from 'node:fs'
import { decode as decodePNG } from './Screenshot.mjs'
const dir = 'build/validation/hybrid-shadow/web'
mkdirSync(dir, { recursive: true })
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage(),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(process.env.NOVA_TEST_URL || 'http://127.0.0.1:5220')
  await page.evaluate(async () => {
    const { createRenderer } = await import(
      '/@fs/Users/eric/Documents/repos/renderer/nova-v2/web/src/Nova.js'
    )
    const c = document.createElement('canvas')
    c.width = 320
    c.height = 240
    document.body.replaceChildren(c)
    window.r = await createRenderer({ canvas: c })
    r.setContinuousRendering(false)
    r.setPostProcessing({ enabled: true, bloom: false, autoExposure: false, toneMapping: 'aces' })
    r.setLighting('pbr')
    await r.loadModel('/generated/DamagedHelmet.glb')
    r.setRenderMode('hybrid')
    r.setHybrid({ shadows: false })
  })
  const decode = (bytes) => {
    const image = decodePNG(bytes),
      pixels = new Uint8Array(image.width * image.height * 4)
    for (let i = 0; i < image.width * image.height; ++i) {
      pixels.set(image.pixels.subarray(i * image.channels, i * image.channels + 3), i * 4)
      pixels[i * 4 + 3] = image.channels === 4 ? image.pixels[i * 4 + 3] : 255
    }
    return pixels
  }
  const capture = async (name) => {
    await page.evaluate(() => r.drawFrame())
    return decode(await page.locator('canvas').screenshot({ path: `${dir}/${name}.png` }))
  }
  const diff = (a, b) => a.reduce((s, v, i) => s + Math.abs(v - b[i]), 0) / a.length
  await capture('single-off')
  await page.evaluate(() => r.setHybrid({ shadows: true }))
  await capture('single-shadow')
  await page.evaluate(async () => {
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
    await r.setScene(
      [{ id: 'box', url: '/generated/SceneRed.glb' }],
      [
        { id: 'ground', assetId: 'box', world: matrix(2, 0.06, 2, 0, -0.8) },
        { id: 'caster', assetId: 'box', world: matrix(0.3, 1, 0.3) },
      ],
    )
    await r.setMaterial(
      0,
      { ...r.getMaterials('box')[0], baseColor: [0.8, 0.8, 0.8, 1], metallic: 0, roughness: 0.7 },
      'box',
    )
    r.setCamera({ yaw: 30, pitch: 25, distance: 5.5 })
    r.setLights([
      {
        id: 'sun',
        type: 'directional',
        direction: [-1, 2, 1],
        intensity: 4,
        castShadow: true,
        shadowSoftness: 0,
      },
      {
        id: 'point',
        type: 'point',
        position: [-2, 2, 1],
        color: [0.2, 0.3, 1],
        intensity: 6,
        castShadow: true,
        shadowSoftness: 0,
      },
      {
        id: 'spot',
        type: 'spot',
        position: [2, 3, 2],
        direction: [-0.3, -1, -0.2],
        color: [1, 0.2, 0.1],
        intensity: 8,
        castShadow: true,
        shadowSoftness: 0,
        innerConeAngle: 0.4,
        outerConeAngle: 0.8,
      },
      {
        id: 'fill',
        type: 'point',
        position: [-2, 2, 1],
        color: [0.2, 0.3, 1],
        intensity: 0.3,
        castShadow: false,
        shadowSoftness: 0,
      },
      {
        id: 'fifth',
        type: 'directional',
        direction: [1, 2, -1],
        intensity: 0.5,
        castShadow: true,
        shadowSoftness: 0,
      },
    ])
    r.setHybrid({ shadows: false })
  })
  const off = await capture('scene-off')
  await page.evaluate(() => r.setHybrid({ shadows: true }))
  const shadow = await capture('scene-shadow')
  if (diff(off, shadow) < 0.1) throw Error('Hybrid shadows missing')
  for (const i of [0, 1, 2, 4]) {
    await page.evaluate((i) => r.setHybrid({ view: 'shadows', shadowLight: i }), i)
    await capture('visibility-' + i)
  }
  await page.evaluate(() => r.setHybrid({ view: 'final' }))
  await capture('scene-final')
  const checks = await page.evaluate(() => {
    const caps = r.getHybridCapabilities()
    let invalid = 0
    for (const v of [{ method: 'taa' }, { method: 'msaa' }, { split: true }])
      try {
        r.setAntiAliasing(v)
      } catch {
        invalid++
      }
    return (
      caps.supported &&
      caps.shadows &&
      caps.softwareRayTracing &&
      !caps.hardwareRayTracing &&
      caps.reflections &&
      caps.globalIllumination &&
      invalid === 3 &&
      r.getRenderMode() === 'hybrid'
    )
  })
  if (!checks) throw Error('Hybrid capability/atomic restriction')
  await page.evaluate(() => {
    r.setRenderMode('raster')
    r.setShadows({ enabled: true, softness: 0, samples: 4 })
  })
  const raster = await capture('raster-shadow')
  if (diff(raster, shadow) > 0.2) throw Error('Hybrid/Raster lighting parity')
  await page.evaluate(() => {
    r.setRenderMode('hybrid')
    r.setHybrid({ ambientOcclusion: true, aoRadius: 2, aoSamples: 16 })
  })
  const ao = await capture('ao-final')
  if (diff(ao, shadow) < 0.05) throw Error('Missing Hybrid AO')
  await page.evaluate(() => r.setHybrid({ view: 'ambient-occlusion' }))
  await capture('ao-visibility')
  await page.evaluate(async () => {
    r.setHybrid({ view: 'final', ambientOcclusion: false, reflections: false })
    await r.setMaterial(0, { ...r.getMaterials('box')[0], metallic: 0.9, roughness: 0.08 }, 'box')
  })
  const reflectionOff = await capture('reflection-off')
  await page.evaluate(() => r.setHybrid({ reflections: true, reflectionSamples: 4 }))
  const reflection = await capture('reflection-final')
  if (diff(reflection, reflectionOff) < 0.05) throw Error('Missing Hybrid reflections')
  await page.evaluate(() => r.setHybrid({ view: 'reflections' }))
  await capture('reflection-only')
  await page.evaluate(() => r.setHybrid({ halfResolutionReflections: true }))
  await capture('reflection-half-only')
  await page.evaluate(() => r.setHybrid({ view: 'final' }))
  const halfReflection = await capture('reflection-half-final')
  if (diff(reflectionOff, halfReflection) < 0.05 || diff(reflection, halfReflection) > 5)
    throw Error('Half-resolution reflection did not preserve reflection lighting')
  await page.evaluate(() => r.setHybrid({ halfResolutionReflections: false }))
  await page.evaluate(() =>
    r.setHybrid({
      view: 'final',
      ambientOcclusion: true,
      aoSamples: 4,
      reflectionSamples: 1,
      temporalReconstruction: true,
    }),
  )
  await page.evaluate(() => r.setHybrid({ historyWeight: 0 }))
  let reference
  for (let f = 1; f <= 16; ++f) {
    const raw = await capture('temporal-raw-' + f)
    reference ||= new Float64Array(raw.length)
    for (let i = 0; i < raw.length; ++i) reference[i] += raw[i] / 16
  }
  await page.evaluate(() => r.setHybrid({ historyWeight: 0.9 }))
  const first = await capture('temporal-first')
  let resolved
  for (let f = 2; f <= 16; ++f) resolved = await capture('temporal-' + f)
  if (
    (await page.evaluate(() => r.getHybridHistoryFrames())) !== 16 ||
    diff(first, resolved) < 0.01
  )
    throw Error('Missing Hybrid temporal history')
  const mse = (a) => a.reduce((sum, v, i) => sum + (v - reference[i]) ** 2, 0) / a.length
  console.log('Temporal reference MSE: first=' + mse(first) + '; resolved=' + mse(resolved))
  if (mse(resolved) >= mse(first))
    throw Error('Temporal reconstruction did not reduce reference error')
  await page.evaluate(() => r.setCamera({ yaw: 32 }))
  await capture('temporal-motion')
  if ((await page.evaluate(() => r.getHybridHistoryFrames())) !== 17)
    throw Error('Small movement lost history')
  await page.evaluate(() => r.setCamera({ yaw: 72 }))
  const cut = await capture('temporal-cut')
  if ((await page.evaluate(() => r.getHybridHistoryFrames())) !== 1)
    throw Error('Camera cut retained history')
  await page.evaluate(() => r.resetHybridHistory())
  if ((await page.evaluate(() => r.getHybridHistoryFrames())) !== 0)
    throw Error('Explicit reset failed')
  const reset = await capture('temporal-reset')
  if (diff(cut, reset) > 0.01) throw Error('Reset reused stale history')
  await page.evaluate(() => {
    const c = r.getViewCamera()
    c.position[0] += 0.1
    r.setViewCamera(c)
  })
  await capture('temporal-full-camera')
  if ((await page.evaluate(() => r.getHybridHistoryFrames())) !== 2)
    throw Error('Full camera movement lost history')
  await page.evaluate(() => r.setViewCamera({ verticalFov: r.getViewCamera().verticalFov + 1 }))
  if ((await page.evaluate(() => r.getHybridHistoryFrames())) !== 0)
    throw Error('Lens change retained history')
  await page.evaluate(() => r.setAntiAliasing({ method: 'ssaa' }))
  for (let f = 0; f < 4; ++f) await capture('temporal-ssaa')
  if ((await page.evaluate(() => r.getHybridHistoryFrames())) !== 4)
    throw Error('Hybrid SSAA history failed')
  await page.evaluate(async () => {
    r.setAntiAliasing({ method: 'off' })
    r.setHybrid({
      shadows: true,
      view: 'final',
      ambientOcclusion: false,
      reflections: false,
      temporalReconstruction: false,
    })
    await r.loadModel(
      '/@fs/Users/eric/Documents/repos/renderer/nova-v2/assets/MaterialSample/Layers.glb',
    )
    r.setLighting('unlit')
    r.setCamera({ yaw: 0, pitch: 0, distance: 3.6, target: [0, 0, 0], exposure: 1 })
  })
  const alphaBase = await capture('temporal-alpha-base')
  await page.evaluate(() => r.setHybrid({ temporalReconstruction: true }))
  let alphaResolved
  for (let f = 0; f < 4; ++f) alphaResolved = await capture('temporal-alpha')
  if (diff(alphaBase, alphaResolved) > 0.05) throw Error('Temporal OIT changed transparency')
  await page.evaluate(async () => {
    const matrix = (x, y, z, tx, ty, tz) => [x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, tx, ty, tz, 1]
    await r.setScene(
      [
        { id: 'mirror', url: '/generated/SceneRed.glb' },
        { id: 'helmet', url: '/generated/DamagedHelmet.glb' },
      ],
      [
        { id: 'ground', assetId: 'mirror', world: matrix(2, 0.06, 2, 0, -0.9, 0) },
        { id: 'subject', assetId: 'helmet', world: matrix(0.65, 0.65, 0.65, 0, 0.05, 0) },
      ],
    )
    r.setLighting('pbr')
    r.setCamera({ yaw: 72, pitch: 25, distance: 5.5 })
    r.setDirectionalLight({ direction: [-1, 1.6, 2], color: [1, 3.2 / 3.5, 0.8], intensity: 3.5 })
    await r.setMaterial(
      0,
      {
        ...r.getMaterials('mirror')[0],
        baseColor: [0.85, 0.85, 0.85, 1],
        metallic: 1,
        roughness: 0.04,
      },
      'mirror',
    )
    r.setHybrid({
      reflections: true,
      reflectionSamples: 1,
      view: 'reflections',
      temporalReconstruction: false,
    })
    await r.setMaterial(
      0,
      { ...r.getMaterials('helmet')[0], normalScale: 1, occlusionStrength: 1 },
      'helmet',
    )
  })
  const mapped = await capture('reflected-material-mapped')
  await page.evaluate(() =>
    r.setMaterial(0, { ...r.getMaterials('helmet')[0], normalScale: 0 }, 'helmet'),
  )
  const flat = await capture('reflected-material-flat')
  await page.evaluate(() =>
    r.setMaterial(0, { ...r.getMaterials('helmet')[0], occlusionStrength: 0 }, 'helmet'),
  )
  const clearAO = await capture('reflected-material-no-ao')
  const groundDelta = (a, b) => {
    let changed = 0
    for (let i = Math.floor(240 * 0.65) * 320 * 4; i < a.length; i += 4)
      for (let c = 0; c < 3; c++) changed += Math.abs(a[i + c] - b[i + c]) > 2
    return changed
  }
  if (groundDelta(mapped, flat) < 20 || groundDelta(flat, clearAO) < 20)
    throw Error('Reflected helmet lost normal map or material AO')
  await page.evaluate(async () => {
    const matrix = (x, y, z, tx, ty, tz) => [x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, tx, ty, tz, 1]
    await r.setScene(
      [
        { id: 'mirror', url: '/generated/SceneRed.glb' },
        { id: 'wall', url: '/generated/SceneRed.glb' },
      ],
      [
        { id: 'ground', assetId: 'mirror', world: matrix(2, 0.06, 2, 0, -0.9, 0) },
        { id: 'wall', assetId: 'wall', world: matrix(2, 0.9, 0.06, 0, 0, -1) },
      ],
    )
    r.setCamera({ yaw: 30, pitch: 25, distance: 5.5 })
    r.setDirectionalLight({ direction: [-1, 1.6, 2], color: [1, 3.2 / 3.5, 0.8], intensity: 1 })
    await r.setMaterial(
      0,
      {
        ...r.getMaterials('mirror')[0],
        baseColor: [0.45, 0.45, 0.45, 1],
        metallic: 0,
        roughness: 0.7,
      },
      'mirror',
    )
    await r.setMaterial(
      0,
      { ...r.getMaterials('wall')[0], baseColor: [1, 0, 0, 1], metallic: 0 },
      'wall',
    )
    r.setHybrid({
      view: 'final',
      reflections: false,
      ambientOcclusion: false,
      diffuseBounce: false,
    })
  })
  const noBounce = await capture('diffuse-bounce-off')
  await page.evaluate(() => r.setHybrid({ diffuseBounce: true }))
  const indirect = await capture('diffuse-bounce-on')
  let colorSpill = 0
  for (let i = Math.floor(240 * 0.65) * 320 * 4; i < indirect.length; i += 4)
    colorSpill += indirect[i] - noBounce[i] > indirect[i + 1] - noBounce[i + 1] + 3
  if (colorSpill < 20) throw Error('Diffuse bounce did not transfer red light to the receiver')
  const parity = []
  for (const api of ['metal', 'vulkan'])
    for (const name of [
      'temporal-ssaa',
      'temporal-alpha-base',
      'temporal-alpha',
      'temporal-first',
      'temporal-16',
      'temporal-motion',
      'temporal-cut',
      'temporal-reset',
      'temporal-full-camera',
      'reflection-off',
      'reflection-final',
      'reflection-only',
      'reflected-material-mapped',
      'reflected-material-flat',
      'reflected-material-no-ao',
      'diffuse-bounce-off',
      'diffuse-bounce-on',
      'ao-final',
      'ao-visibility',
      'single-off',
      'single-shadow',
      'scene-off',
      'scene-shadow',
      'visibility-0',
      'visibility-1',
      'visibility-2',
      'visibility-4',
      'scene-final',
      'raster-shadow',
    ]) {
      const a = await decode(readFileSync(`${dir}/${name}.png`)),
        b = await decode(readFileSync(`build/validation/hybrid-shadow/${api}/${name}.png`)),
        mae = diff(a, b)
      if (mae > 1) throw Error(`${api}/${name} MAE ${mae}`)
      parity.push(mae)
    }
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'Hybrid WebGPU shadows, five lights, capability restrictions and Native/Web parity passed; max MAE ' +
      Math.max(...parity).toFixed(4) +
      '/255',
  )
} finally {
  await browser.close()
}
