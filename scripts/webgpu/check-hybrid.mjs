import { chromium } from 'playwright'
import { mkdirSync, readFileSync } from 'node:fs'
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
  const decode = async (bytes) =>
    page.evaluate(async (base64) => {
      const b = await createImageBitmap(
        new Blob([Uint8Array.from(atob(base64), (c) => c.charCodeAt(0))]),
      )
      const c = new OffscreenCanvas(b.width, b.height),
        ctx = c.getContext('2d')
      ctx.drawImage(b, 0, 0)
      b.close()
      return [...ctx.getImageData(0, 0, c.width, c.height).data]
    }, bytes.toString('base64'))
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
      !caps.reflections &&
      !caps.globalIllumination &&
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
  const parity = []
  for (const api of ['metal', 'vulkan'])
    for (const name of [
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
