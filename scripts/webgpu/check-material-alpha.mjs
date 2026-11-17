import { chromium } from 'playwright'
import { mkdirSync, readFileSync } from 'node:fs'
const dir = 'build/validation/material-alpha/web'
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
    r.setClearColor([0, 0, 0, 1])
    r.setLighting('unlit')
    r.setViewCamera({ position: [0, 0, 3.6] })
    r.setPostProcessing({ enabled: true, bloom: false, autoExposure: false, toneMapping: 'aces' })
    window.load = async (name) =>
      r.loadModel(
        '/@fs/Users/eric/Documents/repos/renderer/nova-v2/assets/MaterialSample/' + name + '.glb',
      )
  })
  async function decode(bytes) {
    return page.evaluate(async (data) => {
      const b = await createImageBitmap(
        new Blob([Uint8Array.from(atob(data), (c) => c.charCodeAt(0))]),
      )
      const c = new OffscreenCanvas(b.width, b.height),
        ctx = c.getContext('2d')
      ctx.drawImage(b, 0, 0)
      b.close()
      return [...ctx.getImageData(0, 0, c.width, c.height).data]
    }, bytes.toString('base64'))
  }
  async function draw(name) {
    await page.evaluate(() => r.drawFrame())
    const png = await page.locator('canvas').screenshot({ path: `${dir}/${name}.png` })
    return decode(png)
  }
  const diff = (a, b) => a.reduce((sum, v, i) => sum + Math.abs(v - b[i]), 0) / a.length
  const energy = (a) => a.reduce((sum, v, i) => sum + (i % 4 === 3 ? 0 : v), 0) / a.length
  const check = (condition, message) => {
    if (!condition) throw Error(message)
  }
  await page.evaluate(() => load('Quad'))
  const front = await draw('front')
  check(energy(front) > 5, 'Front missing')
  await page.evaluate(async () => {
    let m = r.getMaterials()[0]
    m.baseColor[3] = 0
    await r.setMaterial(0, m)
  })
  check(diff(front, await draw('opaque-zero-alpha')) < 0.05, 'OPAQUE alpha used')
  await page.evaluate(() => load('Back'))
  check(energy(await draw('back-single')) < 1, 'Single back visible')
  await page.evaluate(async () => {
    await r.setMaterial(0, { ...r.getMaterials()[0], doubleSided: true })
  })
  check(energy(await draw('back-double')) > 5, 'Double back missing')
  await page.evaluate(() => load('Mirrored'))
  check(diff(front, await draw('mirrored-node')) < 0.05, 'Mirrored node')
  await page.evaluate(() => load('Mask'))
  const mask = await draw('mask')
  await page.evaluate(async () => {
    await r.setMaterial(0, { ...r.getMaterials()[0], alphaCutoff: 0 })
  })
  check(energy(await draw('mask-cutoff-zero')) > energy(mask) * 1.5, 'MASK texture ignored')
  await page.evaluate(async () => {
    let m = r.getMaterials()[0]
    m.alphaCutoff = 0.5
    m.baseColor[3] = 0.2
    await r.setMaterial(0, m)
  })
  check(energy(await draw('mask-factor')) < 1, 'MASK factor ignored')
  check(
    await page.evaluate(async () => {
      const before = r.getMaterials()[0]
      try {
        await r.setMaterial(0, { ...before, alphaCutoff: 2 })
        return false
      } catch {}
      return r.getMaterials()[0].alphaCutoff === before.alphaCutoff
    }),
    'Atomic invalid update',
  )
  await page.evaluate(() => load('Layers'))
  const layers = await draw('layers-forward')
  await page.evaluate(() => load('LayersReverse'))
  check(diff(layers, await draw('layers-reversed')) < 0.1, 'OIT order dependence')
  await page.evaluate(() => r.setRasterSettings({ pipeline: 'deferred' }))
  check(diff(layers, await draw('layers-deferred')) < 0.15, 'Deferred OIT')
  await page.evaluate(() => r.setRasterSettings({ pipeline: 'forward' }))
  for (const method of ['fxaa', 'smaa', 'ssaa']) {
    await page.evaluate((m) => r.setAntiAliasing({ method: m }), method)
    await draw('layers-' + method)
  }
  await page.evaluate(() => r.setAntiAliasing({ method: 'off' }))
  check(
    await page.evaluate(() => {
      for (const value of [{ method: 'msaa' }, { method: 'taa' }, { split: true }]) {
        try {
          r.setAntiAliasing(value)
          return false
        } catch {}
      }
      try {
        r.setRenderMode('ray-tracing')
        return false
      } catch {}
      return !r.getCapabilities().pathTracing && r.getMaterialCapabilities().maskShadows
    }),
    'Capability restrictions',
  )
  await page.evaluate(async () => {
    window.matrix = (x, y, z, tx = 0, ty = 0, tz = 0) => [
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
      [
        {
          id: 'quad',
          url: '/@fs/Users/eric/Documents/repos/renderer/nova-v2/assets/MaterialSample/Quad.glb',
          sourceSpace: true,
        },
      ],
      [{ id: 'mirror', assetId: 'quad', world: matrix(-1, 1, 1) }],
    )
  })
  check(diff(front, await draw('mirrored-instance')) < 0.05, 'Mirrored instance')
  await page.evaluate(async () => {
    await r.setScene(
      [
        {
          id: 'receiver',
          url: '/@fs/Users/eric/Documents/repos/renderer/nova-v2/assets/MaterialSample/Quad.glb',
          sourceSpace: true,
        },
        {
          id: 'caster',
          url: '/@fs/Users/eric/Documents/repos/renderer/nova-v2/assets/MaterialSample/Mask.glb',
          sourceSpace: true,
        },
      ],
      [
        { id: 'receiver', assetId: 'receiver', world: matrix(1.2, 1.2, 1, 0, 0, -0.5) },
        { id: 'caster', assetId: 'caster', world: matrix(0.45, 0.45, 1, 0, 0, 0.3) },
      ],
    )
    for (const id of ['receiver', 'caster'])
      await r.setMaterial(0, { ...r.getMaterials(id)[0], baseColor: [1, 1, 1, 1] }, id)
    r.setLighting('pbr')
    r.setLights([
      {
        id: 'sun',
        type: 'directional',
        direction: [1, 0, 1],
        intensity: 4,
        castShadow: true,
        shadowSoftness: 0,
      },
    ])
    r.setShadows({ enabled: false })
  })
  const shadowOff = await draw('shadow-off')
  await page.evaluate(() => r.setShadows({ enabled: true, softness: 0, samples: 1 }))
  const shadowMask = await draw('shadow-mask')
  await page.evaluate(async () =>
    r.setMaterial(0, { ...r.getMaterials('caster')[0], alphaCutoff: 0 }, 'caster'),
  )
  const shadowSolid = await draw('shadow-solid')
  check(
    diff(shadowOff, shadowMask) > 0.1 && diff(shadowMask, shadowSolid) > 0.1,
    'MASK shadow holes missing',
  )
  const parity = []
  for (const api of ['metal', 'vulkan'])
    for (const name of [
      'front',
      'back-single',
      'back-double',
      'mirrored-node',
      'mask',
      'mask-cutoff-zero',
      'mask-factor',
      'layers-forward',
      'layers-reversed',
      'layers-deferred',
      'mirrored-instance',
      'shadow-off',
      'shadow-mask',
      'shadow-solid',
    ]) {
      const a = await decode(readFileSync(`${dir}/${name}.png`)),
        b = await decode(readFileSync(`build/validation/material-alpha/${api}/${name}.png`))
      const mae = diff(a, b)
      check(mae < 1, `${api}/${name} MAE ${mae}`)
      parity.push(mae)
    }
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    `WebGPU alpha coverage, sidedness, OIT, runtime updates, AA restrictions passed; native/Web max MAE ${Math.max(...parity).toFixed(4)}/255`,
  )
} finally {
  await browser.close()
}
