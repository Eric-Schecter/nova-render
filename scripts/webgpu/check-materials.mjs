import { chromium } from 'playwright'
import { realpathSync, mkdirSync, writeFileSync } from 'node:fs'
import { decode } from './Screenshot.mjs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5220'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
mkdirSync('build/validation/materials', { recursive: true })
try {
  const page = await browser.newPage({ viewport: { width: 800, height: 700 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(base)
  await page.evaluate(
    async (root) => {
      const Nova = await import(root + '/web/src/Nova.js')
      const imageCanvas = new OffscreenCanvas(1, 1),
        ctx = imageCanvas.getContext('2d')
      ctx.fillStyle = '#ff0000'
      ctx.fillRect(0, 0, 1, 1)
      const imageURL = URL.createObjectURL(await imageCanvas.convertToBlob({ type: 'image/png' }))
      const decoded = await Nova.loadMaterialImage(imageURL)
      URL.revokeObjectURL(imageURL)
      if (decoded.width !== 1 || decoded.height !== 1 || decoded.pixels.join(',') !== '255,0,0,255')
        throw Error('Material image decoding failed')
      document.body.innerHTML = '<canvas style="width:480px;height:360px"></canvas>'
      const canvas = document.querySelector('canvas'),
        renderer = await Nova.createRenderer({ canvas, shaderDirectory: '/generated/' })
      window.renderer = renderer
      await renderer.loadModel('/generated/MaterialDual.glb')
      renderer.setViewCamera({
        position: [0, 0, 4],
        projection: 'orthographic',
        orthographicHeight: 2.6,
      })
      renderer.setAntiAliasing({ method: 'fxaa' })
      await renderer.drawFrame()
      if (renderer.getMaterials().length !== 2) throw Error('Missing GLB slots')
    },
    '/@fs' + realpathSync('.'),
  )
  async function inspect(name) {
    const png = await page.locator('canvas').screenshot()
    writeFileSync(`build/validation/materials/${name}.png`, png)
    const image = decode(png),
      counts = [0, 0, 0, 0, 0, 0]
    for (let y = 0; y < image.height; y++)
      for (let x = 0; x < image.width; x++) {
        const p = (y * image.width + x) * image.channels
        for (let c = 0; c < 3; c++)
          if (
            image.pixels[p + c] > 120 &&
            image.pixels[p + c] > 2 * image.pixels[p + ((c + 1) % 3)] &&
            image.pixels[p + c] > 2 * image.pixels[p + ((c + 2) % 3)]
          )
            counts[(x < image.width / 2 ? 0 : 3) + c]++
      }
    return counts
  }
  let counts = await inspect('initial')
  if (counts[0] < 500 || counts[5] < 500) throw Error('Initial independent slots missing ' + counts)
  await page.evaluate(async () => {
    renderer.setFog({ mode: 'linear', color: [0, 1, 0], nearDistance: 0, farDistance: 1 })
    await renderer.drawFrame()
  })
  counts = await inspect('fog-linear')
  if (counts[1] < 500 || counts[4] < 500) throw Error('Linear fog missing ' + counts)
  await page.evaluate(async () => {
    renderer.setFog({ mode: 'exp2', density: 1 })
    await renderer.drawFrame()
    let rejected = false
    try {
      renderer.setFog({ farDistance: 0 })
    } catch {
      rejected = true
    }
    if (!rejected || renderer.getFog().density !== 1)
      throw Error('Fog validation not transactional')
    renderer.setFog({ mode: 'off' })
    await renderer.drawFrame()
  })
  await page.evaluate(async () => {
    const m = renderer.getMaterials()[0]
    m.baseColor = [0, 1, 0, 1]
    m.metallic = 0.75
    m.roughness = 0.2
    await renderer.setMaterial(0, m)
    await renderer.drawFrame()
    if (Math.abs(renderer.getMaterials()[0].roughness - 0.2) > 1e-6)
      throw Error('Factors not retained')
  })
  counts = await inspect('green')
  if (counts[1] < 500 || counts[5] < 500) throw Error('Slot update leaked ' + counts)
  await page.evaluate(async () => {
    const good = renderer.getMaterials()[0]
    for (const bad of [
      { ...good, roughness: -1 },
      { ...good, metallic: NaN },
      { ...good, baseColorTexture: { width: 2, height: 2, pixels: new Uint8Array(4) } },
    ]) {
      let rejected = false
      try {
        await renderer.setMaterial(0, bad)
      } catch {
        rejected = true
      }
      if (!rejected) throw Error('Invalid material accepted')
    }
    if (renderer.getMaterials()[0].baseColor[1] !== 1)
      throw Error('Failed replacement changed state')
    const copy = renderer.getMaterials()
    copy[0].baseColor[1] = 0
    if (renderer.getMaterials()[0].baseColor[1] !== 1) throw Error('Getter leaked mutable state')
    good.baseColor = [1, 1, 1, 1]
    good.baseColorTexture = { width: 1, height: 1, pixels: new Uint8Array([255, 0, 0, 255]) }
    const pending = renderer.setMaterial(0, good)
    good.baseColorTexture.pixels[0] = 0
    await pending
    await renderer.drawFrame()
  })
  counts = await inspect('texture')
  if (counts[0] < 500 || counts[5] < 500) throw Error('Texture edit failed ' + counts)
  await page.evaluate(async () => {
    const m = renderer.getMaterials()[0]
    m.baseColorTexture = null
    await renderer.setMaterial(0, m)
    for (const method of ['off', 'fxaa', 'smaa', 'msaa', 'ssaa', 'taa']) {
      renderer.setAntiAliasing({ method })
      await renderer.drawFrame()
    }
  })
  counts = await inspect('removed')
  if (counts[0] !== 0 || counts[5] < 500) throw Error('Texture removal or AA failed ' + counts)
  await page.evaluate(async () => {
    await renderer.setScene(
      [{ id: 'dual', url: '/generated/MaterialDual.glb' }],
      [{ id: 'a', assetId: 'dual' }],
    )
    const hit = renderer.pickScene({ origin: [-0.6, 0, 4], direction: [0, 0, -1] })
    if (!hit) throw Error('Missing picking hit')
    const m = renderer.getMaterials('dual')[1]
    m.baseColor = [0, 1, 0, 1]
    await renderer.setMaterial(1, m, 'dual')
    if (
      renderer.pickScene({ origin: [-0.6, 0, 4], direction: [0, 0, -1] }).distance !== hit.distance
    )
      throw Error('Material edit changed picking')
    await renderer.setScene(
      [{ id: 'dual', url: '/generated/MaterialDual.glb' }],
      [{ id: 'a', assetId: 'dual' }],
    )
    if (renderer.getMaterials('dual')[1].baseColor[1] !== 1)
      throw Error('Transform update lost material edit')
    await renderer.drawFrame()
  })
  await page.evaluate(async () => {
    const before = renderer.pickScene({ origin: [-0.6, 0, 4], direction: [0, 0, -1] })
    await renderer.loadEnvironment('/generated/studio_small_09.bin')
    await renderer.drawFrame()
    await renderer.loadEnvironment(null)
    await renderer.drawFrame()
    if (
      renderer.pickScene({ origin: [-0.6, 0, 4], direction: [0, 0, -1] }).distance !==
        before.distance ||
      renderer.getMaterials('dual').length !== 2
    )
      throw Error('Environment clearing changed scene state')
  })
  counts = await inspect('scene')
  if (counts[0] < 500 || counts[4] < 500) throw Error('Scene asset edit failed ' + counts)
  await page.evaluate(async () => {
    await renderer.loadModel('/generated/MaterialDual.glb')
    renderer.setLighting('pbr')
    renderer.setAntiAliasing({ method: 'off' })
    renderer.setPathTracing({ resolutionScale: 1, maxBounces: 2, samplesPerFrame: 4 })
    await renderer.setRenderMode('ray-tracing')
    await renderer.drawFrame()
  })
  counts = await inspect('ray')
  if (counts[0] < 200 || counts[5] < 200) throw Error('Ray tracer lost slots ' + counts)
  await page.evaluate(() => renderer.dispose())
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS WebGPU material slots, factors/maps, validation, ownership, all AA, scene/picking and ray tracing',
  )
} finally {
  await browser.close()
}
