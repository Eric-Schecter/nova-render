import { chromium } from 'playwright'
import { realpathSync, mkdirSync, writeFileSync } from 'node:fs'
import { inflateSync } from 'node:zlib'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5210'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
mkdirSync('build/validation/scene', { recursive: true })
try {
  const page = await browser.newPage({ viewport: { width: 1000, height: 850 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(base)
  await page.evaluate(
    async (root) => {
      const require = (value, message) => {
        if (!value) throw Error(message)
      }
      const { SceneData } = await import(root + '/web/src/SceneData.js')
      const { createRenderer } = await import(root + '/web/src/Nova.js')
      const assets = [
        { id: 'red', url: '/generated/SceneRed.glb' },
        { id: 'blue', url: '/generated/SceneBlue.glb' },
      ]
      const instances = [
        { id: 'upper', assetId: 'red' },
        { id: 'lower', assetId: 'red' },
        { id: 'right', assetId: 'blue' },
      ].map((i, k) => ({
        ...i,
        world: [
          0.55,
          0,
          0,
          0,
          0,
          0.55,
          0,
          0,
          0,
          0,
          0.55,
          0,
          k === 2 ? 1.2 : -1.2,
          k === 0 ? 0.8 : k === 1 ? -0.8 : 0,
          0,
          1,
        ],
      }))
      const scene = new SceneData(),
        first = await scene.update(assets, instances)
      require(first.model.vertices.length === 72 * 12 &&
        first.draws.length === 2 &&
        first.instances.length ===
          144 + first.model.materialRecords.length, 'Shared geometry/group layout')
      require(scene.assetBounds('red').maximum[0] === 1, 'Normalized bounds')
      const sourceAssets = assets.map((a) => ({ ...a, sourceSpace: true }))
      const sourceInstances = instances.map((i) => ({
        ...i,
        world: i.world.map((v, k) =>
          [0, 5, 10].includes(k) ? v * 0.5 : k === 12 ? v - 0.55 : k === 13 ? v - 0.275 : v,
        ),
      }))
      const second = await scene.update(sourceAssets, sourceInstances)
      require(first.model === second.model, 'Space-only update rebuilt geometry')
      require(scene.assetBounds('red').maximum[0] === 4, 'Source bounds lost node transforms')
      require(first.instances.every(
        (v, k) => Math.abs(v - second.instances[k]) < 1e-6,
      ), 'Equivalent world transform differs')
      let requests = 0
      const original = window.fetch
      window.fetch = (...args) => {
        if (String(args[0]).endsWith('.glb')) requests++
        return original(...args)
      }
      const canvas = document.createElement('canvas')
      canvas.style = 'width:640px;height:480px'
      document.body.replaceChildren(canvas)
      const r = await createRenderer({ canvas, shaderDirectory: '/generated/' })
      r.setContinuousRendering(false)
      r.setClearColor([0, 0, 0, 1])
      r.setCamera({ distance: 7 })
      await r.setScene(assets, instances)
      require(requests === 2, 'Expected one model load per asset')
      window.sceneTest = {
        r,
        assets,
        instances,
        sourceAssets,
        sourceInstances,
        requests: () => requests,
        require,
      }
    },
    '/@fs' + realpathSync('.'),
  )
  function decode(png) {
    const width = png.readUInt32BE(16),
      height = png.readUInt32BE(20),
      channels = png[25] === 6 ? 4 : 3
    if (png[24] !== 8 || ![2, 6].includes(png[25]) || png[28] !== 0)
      throw Error('Unsupported screenshot PNG')
    const chunks = []
    for (let offset = 8; offset < png.length; ) {
      const size = png.readUInt32BE(offset)
      if (png.toString('ascii', offset + 4, offset + 8) === 'IDAT')
        chunks.push(png.subarray(offset + 8, offset + 8 + size))
      offset += size + 12
    }
    const raw = inflateSync(Buffer.concat(chunks)),
      stride = width * channels,
      pixels = Buffer.alloc(height * stride)
    for (let y = 0; y < height; y++) {
      const filter = raw[y * (stride + 1)]
      for (let x = 0; x < stride; x++) {
        const k = y * stride + x,
          a = x >= channels ? pixels[k - channels] : 0,
          b = y ? pixels[k - stride] : 0,
          c = y && x >= channels ? pixels[k - stride - channels] : 0
        const p = a + b - c,
          pa = Math.abs(p - a),
          pb = Math.abs(p - b),
          pc = Math.abs(p - c)
        const predictor =
          filter === 0
            ? 0
            : filter === 1
              ? a
              : filter === 2
                ? b
                : filter === 3
                  ? Math.floor((a + b) / 2)
                  : filter === 4
                    ? pa <= pb && pa <= pc
                      ? a
                      : pb <= pc
                        ? b
                        : c
                    : NaN
        if (!Number.isFinite(predictor)) throw Error('Invalid PNG filter')
        pixels[k] = (raw[y * (stride + 1) + x + 1] + predictor) & 255
      }
    }
    return { width, height, channels, pixels }
  }
  const inspect = async (empty = false) => {
    // Browser screenshots retain the presented image across async AA setup.
    const png = await page.locator('canvas').screenshot(),
      image = decode(png)
    const { width, height, channels, pixels } = image
    let upper = 0,
      lower = 0,
      blue = 0
    for (let y = 0; y < height; y++)
      for (let x = 0; x < width; x++) {
        const k = (y * width + x) * channels,
          r = pixels[k],
          b = pixels[k + 2]
        if (r > 150 && r > b * 2 && x < width / 2) {
          if (y < height / 2) upper++
          else lower++
        }
        if (b > 150 && b > r * 2 && x > width / 2) blue++
      }
    if (empty ? upper + lower + blue !== 0 : Math.min(upper, lower, blue) < 50)
      throw Error('Invalid rendered scene ' + JSON.stringify({ upper, lower, blue }))
    return { png, upper, lower, blue }
  }
  const results = []
  for (const method of ['off', 'fxaa', 'smaa', 'msaa', 'ssaa', 'taa']) {
    await page.evaluate(async (method) => {
      const { r } = window.sceneTest
      r.setAntiAliasing({ method })
      await r.drawFrame()
    }, method)
    const { png, ...counts } = await inspect()
    results.push({ method, ...counts })
    writeFileSync('build/validation/scene/webgpu-' + method + '.png', png)
  }
  const normalized = (await inspect()).png
  await page.evaluate(async () => {
    const { r, sourceAssets, sourceInstances, requests, require } = window.sceneTest
    await r.setScene(sourceAssets, sourceInstances)
    require(requests() === 2, 'Space update reloaded GLBs')
    await r.drawFrame()
  })
  const source = (await inspect()).png
  if (!decode(normalized).pixels.equals(decode(source).pixels))
    throw Error('Source-space equivalent image differs')
  await page.evaluate(async () => {
    const { r, assets, instances, sourceAssets, sourceInstances, requests, require } =
      window.sceneTest
    await r.setScene(
      sourceAssets,
      sourceInstances.map((i) => ({ ...i, world: new Float32Array(i.world) })),
    )
    const moved = sourceInstances.map((i) => ({ ...i, world: [...i.world] }))
    moved[0].world[12] += 0.2
    await r.setScene(sourceAssets, moved)
    await r.drawFrame()
    require(requests() === 2, 'Transform update reloaded models')
    require(r.getCapabilities().rayTracing, 'Scene ray capability missing')
    let failures = 0
    for (const [a, i] of [
      [assets, [instances[0], instances[0]]],
      [assets, [{ ...instances[0], assetId: 'missing' }]],
      [assets, [{ ...instances[0], world: [NaN, ...instances[0].world.slice(1)] }]],
      [assets.map((a) => ({ ...a, sourceSpace: 'yes' })), instances],
    ]) {
      try {
        await r.setScene(a, i)
      } catch {
        failures++
      }
    }
    require(failures === 4, 'Invalid scene accepted')
    require(r.getSceneAssetBounds('red').maximum[0] === 4, 'Failed update changed current space')
    await r.drawFrame()
  })
  await inspect()
  await page.evaluate(async () => {
    const { r } = window.sceneTest
    r.setPostProcessing({ enabled: true, bloom: false, autoExposure: false })
    await r.drawFrame()
  })
  await inspect()
  await page.evaluate(async () => {
    const { r, require } = window.sceneTest
    r.setPostProcessing({ enabled: false })
    await r.setScene([], [])
    await r.drawFrame()
    require(r.getSceneAssetBounds('red') === null, 'Stale bounds')
  })
  await inspect(true)
  await page.evaluate(async () => {
    const { r, assets, sourceAssets, sourceInstances, require } = window.sceneTest
    await r.loadModel(assets[0].url)
    await r.drawFrame()
    require(r.getSceneAssetBounds('red') === null, 'Legacy load retained scene')
    await r.setScene(sourceAssets, sourceInstances)
    await r.drawFrame()
  })
  await inspect()
  await page.evaluate(() => window.sceneTest.r.dispose())
  await page.goto(base + '/examples/scene')
  await page.locator('[data-ready="true"]').waitFor({ timeout: 120000 })
  await page.locator('canvas').scrollIntoViewIfNeeded()
  await page.screenshot({ path: 'build/validation/scene/page.png' })
  await page.setViewportSize({ width: 390, height: 844 })
  if (await page.evaluate(() => document.documentElement.scrollWidth > innerWidth))
    throw Error('Mobile overflow')
  if (errors.length) throw Error(errors.join('\n'))
  writeFileSync('build/validation/scene/webgpu.json', JSON.stringify(results, null, 2))
  console.log('PASS WebGPU scene API:', results)
} finally {
  await browser.close()
}
