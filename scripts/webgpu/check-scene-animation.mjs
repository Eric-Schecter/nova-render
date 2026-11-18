import { chromium } from 'playwright'
import { mkdirSync, readFileSync } from 'node:fs'
import { decode as decodePNG } from './Screenshot.mjs'
const dir = 'build/validation/scene-animation/web'
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
    r.setLighting('unlit')
    window.sources = [
      { id: 'rig', url: '/generated/SkinningRobot.glb' },
      { id: 'box', url: '/generated/SceneRed.glb' },
    ]
    window.world = (x, y, z) => [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1]
    window.instances = [
      { id: 'a', assetId: 'rig', world: world(-1.2, 0, 0) },
      { id: 'b', assetId: 'rig', world: world(1.2, 0, 0) },
      { id: 'static', assetId: 'box', world: world(0, -2, 0) },
    ]
    await r.setScene(sources, instances)
    r.setCamera({ distance: 6, yaw: 0, pitch: 0 })
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
  const base = await capture('bind')
  const query = () =>
    page.evaluate(() => ({
      bounds: r.getSceneInstanceBounds('b'),
      hits: Array.from({ length: 400 }, (_, i) => {
        const hit = r.pickScene({
          origin: [-0.1 + (i % 20) * 0.14, -1.4 + Math.floor(i / 20) * 0.15, 10],
          direction: [0, 0, -1],
          includeIds: ['b'],
        })
        return hit ? hit.distance : -1
      }),
    }))
  const bindQuery = await query()
  const clips = await page.evaluate(() => r.getAnimations('rig'))
  if (clips.length !== 2 || (await page.evaluate(() => r.getAnimationState('a').playing)))
    throw Error('Missing clips/manual clock')
  await page.evaluate(
    (duration) => r.setAnimationState('b', { clip: 1, time: duration * 0.4, playing: false }),
    clips[1].duration,
  )
  const posed = await capture('pose')
  const poseQuery = await query()
  if (
    !poseQuery.hits.some((v, i) => Math.abs(v - bindQuery.hits[i]) > 1e-5) ||
    JSON.stringify(poseQuery.bounds) === JSON.stringify(bindQuery.bounds)
  )
    throw Error('Pose picking/bounds stayed in bind pose')
  let left = 0,
    right = 0
  for (let y = 0; y < 240; ++y)
    for (let x = 0; x < 320; ++x)
      for (let c = 0; c < 3; ++c) {
        const d = Math.abs(base[(y * 320 + x) * 4 + c] - posed[(y * 320 + x) * 4 + c])
        if (x < 160) left += d
        else right += d
      }
  if (left !== 0 || right < 100)
    throw Error('GPU poses missing or cross-contaminated ' + left + '/' + right)
  await page.evaluate(() => {
    r.setAnimationState('b', { playing: true, speed: 2 })
    r.updateAnimations(0.1)
  })
  await capture('advanced')
  if ((await page.evaluate(() => r.getAnimationState('a').time)) !== 0)
    throw Error('Instance state contamination')
  await page.evaluate((duration) => {
    r.setAnimationState('b', { time: duration, playing: false })
    r.updateAnimations(0)
  }, clips[1].duration)
  const before = await page.evaluate(() => r.getAnimationState('b'))
  await page.evaluate(async () => {
    for (const delta of [-1, NaN]) {
      let failed = false
      try {
        r.updateAnimations(delta)
      } catch {
        failed = true
      }
      if (!failed) throw Error('Invalid clock accepted')
    }
    let failed = false
    try {
      r.setAnimationState('b', { speed: 5 })
    } catch {
      failed = true
    }
    if (!failed) throw Error('Invalid speed accepted')
    instances.push({ id: 'extra', assetId: 'box', world: world(10, 0, 0) })
    await r.setScene(sources, instances)
    await r.setMaterial(0, { ...r.getMaterials('box')[0], roughness: 0.4 }, 'box')
  })
  if (
    JSON.stringify(before) !== JSON.stringify(await page.evaluate(() => r.getAnimationState('b')))
  )
    throw Error('SetScene/material edit lost state')
  const caps = await page.evaluate(() => r.getAnimationCapabilities())
  if (
    !caps.gpuSkinning ||
    !caps.posedPicking ||
    !caps.posedBounds ||
    !caps.dynamicShadows ||
    caps.morph
  )
    throw Error('False capabilities')
  await page.evaluate(() => {
    for (const mode of ['hybrid', 'ray-tracing']) {
      let failed = false
      try {
        r.setRenderMode(mode)
      } catch {
        failed = true
      }
      if (!failed || r.getRenderMode() !== 'raster') throw Error('Animated ray mode accepted')
    }
  })
  await capture('preserved')
  await page.evaluate(async () => {
    instances.shift()
    await r.setScene(sources, instances)
    let failed = false
    try {
      r.getAnimationState('a')
    } catch {
      failed = true
    }
    if (!failed) throw Error('Removed state retained')
  })
  await page.evaluate(async () => {
    await r.setScene(
      [
        {
          id: 'node',
          url: '/@fs/Users/eric/Documents/repos/renderer/nova-v2/tests/fixtures/scene-animation/Node.glb',
        },
      ],
      [
        { id: 'node-a', assetId: 'node', world: world(-1.2, 0, 0) },
        { id: 'node-b', assetId: 'node', world: world(1.2, 0, 0) },
      ],
    )
    if (r.getAnimations('node').length !== 2) throw Error('Missing node clips')
  })
  const nodeBind = await capture('node-bind'),
    nodeBefore = await page.evaluate(() => r.getSceneInstanceBounds('node-b'))
  await page.evaluate(() => r.setAnimationState('node-b', { time: 0.5, playing: false }))
  const nodePose = await capture('node-pose'),
    nodeAfter = await page.evaluate(() => r.getSceneInstanceBounds('node-b'))
  if (
    diff(nodeBind, nodePose) < 0.01 ||
    nodeAfter.minimum[0] <= nodeBefore.minimum[0] + 0.1 ||
    (await page.evaluate(() => r.getAnimationState('node-a').time)) !== 0
  )
    throw Error('Rigid node animation failed')
  await page.evaluate(() => {
    const floor = world(0, -1.4, 0)
    floor[0] = floor[10] = 3
    floor[5] = 0.15
    window.shadowScene = [
      { id: 'b', assetId: 'rig' },
      { id: 'floor', assetId: 'box', world: floor, castShadow: false },
    ]
    r.setCamera({ distance: 6, yaw: 0, pitch: 25 })
    r.setDirectionalLight({ direction: [-1, 2, 1], intensity: 8, color: [1, 1, 1] })
    r.setLighting('pbr')
    r.setShadows({ enabled: true, softness: 0 })
  })
  async function shadowMask(name, seek = false) {
    await page.evaluate(async (seek) => {
      shadowScene[0].castShadow = true
      await r.setScene(sources, shadowScene)
      if (seek)
        r.setAnimationState('b', { clip: 1, time: r.getAnimations('rig')[1].duration * 0.4 })
    }, seek)
    const on = await capture(name)
    await page.evaluate(async () => {
      shadowScene[0].castShadow = false
      await r.setScene(sources, shadowScene)
    })
    const off = await capture(name + '-off')
    return on.map((value, index) => (off[index] > value + 8 ? 255 : 0))
  }
  const shadowBind = await shadowMask('shadow-bind')
  const shadowPose = await shadowMask('shadow-pose', true)
  if (shadowPose.filter(Boolean).length < 40 || diff(shadowBind, shadowPose) < 0.03)
    throw Error('Animated shadow did not follow pose or cast flag')
  await page.evaluate(() => r.loadModel(sources[0].url))
  const singleMasks = []
  for (const [name, time] of [
    ['single-shadow-bind', 0],
    ['single-shadow-pose', clips[1].duration * 0.4],
  ]) {
    await page.evaluate((time) => {
      r.setShadows({ enabled: true })
      r.setAnimationState({ clip: 1, time, speed: 1, playing: false, loop: true })
    }, time)
    const on = await capture(name)
    await page.evaluate(() => r.setShadows({ enabled: false }))
    const off = await capture(name + '-off')
    const mask = on.map((v, i) => (off[i] > v + 8 ? 255 : 0))
    if (!mask.some(Boolean)) throw Error('Single-model self-shadow missing')
    singleMasks.push(mask)
  }
  if (diff(...singleMasks) <= 0.001) throw Error('Single-model shadow did not follow the pose')
  const parity = []
  for (const api of ['metal', 'vulkan'])
    for (const name of [
      'bind',
      'pose',
      'advanced',
      'preserved',
      'node-bind',
      'node-pose',
      'single-shadow-bind',
      'single-shadow-pose',
    ]) {
      const a = await decode(readFileSync(`${dir}/${name}.png`)),
        b = await decode(readFileSync(`build/validation/scene-animation/${api}/${name}.png`)),
        mae = diff(a, b)
      if (mae > 1) throw Error(`${api}/${name} MAE ${mae}`)
      parity.push(mae)
    }
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'Scene animation GPU poses, manual clock, preservation and Native/Web parity passed; max MAE ' +
      Math.max(...parity).toFixed(4) +
      '/255',
  )
} finally {
  await browser.close()
}
