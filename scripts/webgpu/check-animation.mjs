import { chromium } from 'playwright'
import { readFile, mkdir } from 'node:fs/promises'
import { resolve } from 'node:path'
const base = process.env.NOVA_PREVIEW_URL || 'http://127.0.0.1:5173'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 1000 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  if (!process.env.NOVA_PREVIEW_URL) {
    const bytes = await readFile('build/animation-native-pose.bin'),
      native = Array.from(new Float32Array(bytes.buffer, bytes.byteOffset, bytes.byteLength / 4))
    const foxBytes = await readFile('build/fox-native-pose.bin'),
      foxNative = Array.from(
        new Float32Array(foxBytes.buffer, foxBytes.byteOffset, foxBytes.byteLength / 4),
      )
    await page.goto(base + '/')
    console.log(
      await page.evaluate(
        async ({ root, native, foxNative, probeURL }) => {
          const { loadGLB } = await import(root + '/ModelData.js'),
            { createAnimationPlayer } = await import(root + '/AnimationData.js'),
            Nova = await import(root + '/Nova.js'),
            { createSkinReadback } = await import(probeURL)
          const data = await loadGLB('/generated/SkinningRobot.glb')
          const device = await (await navigator.gpu.requestAdapter()).requestDevice()
          device.pushErrorScope('validation')
          try {
            const bind = data.vertices.slice(),
              player = createAnimationPlayer(data)
            const readSkin = await createSkinReadback(device)
            if (player.clips.length !== 2) throw Error('Clips missing')
            player.set({ ...player.state, time: 0.5, playing: false })
            const gpu = await readSkin(data)
            let maximum = 0
            for (let i = 0; i < native.length; i++)
              maximum = Math.max(maximum, Math.abs(gpu[i] - native[i]))
            if (gpu.length !== native.length || maximum > 3e-5)
              throw Error('Native/web pose mismatch ' + maximum)
            if (data.vertices.some((v, i) => v !== bind[i]))
              throw Error('CPU changed bind vertices')
            const snapshot = gpu
            player.update(0.5)
            const pausedGpu = await readSkin(data)
            if (pausedGpu.some((v, i) => v !== snapshot[i])) throw Error('Paused pose advanced')
            player.set({ ...player.state, clip: 1 })
            const wave = await readSkin(data)
            if (!wave.some((v, i) => Math.abs(v - snapshot[i]) > 0.01))
              throw Error('Clips identical')
            let rejected = false
            try {
              player.set({ ...player.state, speed: NaN })
            } catch {
              rejected = true
            }
            if (!rejected) throw Error('Invalid animation state accepted')
            const host = document.createElement('div'),
              canvas = document.createElement('canvas')
            host.style = 'position:fixed;inset:0;background:#111;z-index:9999'
            canvas.style = 'width:600px;height:500px'
            host.append(canvas)
            document.body.append(host)
            const r = await Nova.createRenderer({ canvas })
            window.animationTest = r
            window.animationCanvas = canvas
            await r.loadModel('/generated/SkinningRobot.glb')
            r.setLighting('pbr')
            r.setCamera({ yaw: 15, pitch: 5 })
            r.setAnimationState({ ...r.getAnimationState(), playing: false })
            await r.drawFrame()
            const first = canvas.toDataURL()
            const writes = [],
              originalWrite = GPUQueue.prototype.writeBuffer
            GPUQueue.prototype.writeBuffer = function (buffer, ...args) {
              writes.push({ label: buffer.label, bytes: args[1]?.byteLength })
              return originalWrite.call(this, buffer, ...args)
            }
            r.setAnimationState({ ...r.getAnimationState(), time: 0.5 })
            await r.drawFrame()
            GPUQueue.prototype.writeBuffer = originalWrite
            if (
              writes.some(
                (w) => w.label === 'Nova bind vertices' || w.label === 'Nova skin inputs',
              ) ||
              !writes.some((w) => w.label === 'Nova joint palette')
            )
              throw Error('Per-frame uploads must update only joint palette and uniforms')
            if (canvas.toDataURL() === first) throw Error('Skinned GPU pixels did not move')
            if (r.getCapabilities().pathTracing)
              throw Error('Animated path mode must report unsupported')
            r.setAnimationState({ ...r.getAnimationState(), playing: true })
            await r.drawFrame()
            await new Promise((resolve) => setTimeout(resolve, 300))
            if (r.getAnimationState().time === 0.5) throw Error('Automatic playback failed')
            r.setAnimationState({ ...r.getAnimationState(), playing: false })
            const paused = r.getAnimationState().time
            await new Promise((resolve) => setTimeout(resolve, 100))
            if (r.getAnimationState().time !== paused) throw Error('Pause failed')
            const error = await device.popErrorScope()
            if (error) throw Error(error.message)
            device.pushErrorScope('validation')
            await r.loadModel('/generated/Fox.glb')
            if (r.getAnimations().length !== 3) throw Error('Fox clips missing')
            r.setCamera({ yaw: 65, pitch: 10, distance: 3 })
            r.setAnimationState({ clip: 0, time: 0.5, speed: 1, playing: false, loop: true })
            const fox = await loadGLB('/generated/Fox.glb')
            try {
              const foxPlayer = createAnimationPlayer(fox)
              foxPlayer.set({ ...foxPlayer.state, time: 0.5, playing: false })
              const actual = await readSkin(fox)
              let difference = 0
              for (let i = 0; i < actual.length; i++)
                difference = Math.max(difference, Math.abs(actual[i] - foxNative[i]))
              if (actual.length !== foxNative.length || difference > 3e-5)
                throw Error('Fox native/web GPU pose mismatch ' + difference)
            } finally {
              fox.images.forEach((i) => i.bitmap?.close())
            }
            await r.loadEnvironment('/generated/venice_sunset.bin')
            r.setDirectionalLight({ direction: [-1, 1.6, 2], color: [1, 1, 1], intensity: 0 })
            await r.drawFrame()
            const sky = canvas.toDataURL()
            r.setEnvironment({ ...r.getEnvironment(), showBackground: false })
            await r.drawFrame()
            const lit = canvas.toDataURL()
            if (lit === sky) throw Error('Fox sky visibility had no effect')
            r.setEnvironment({ ...r.getEnvironment(), intensity: 0 })
            await r.drawFrame()
            if (canvas.toDataURL() === lit) throw Error('Fox environment light had no effect')
            r.setEnvironment({ ...r.getEnvironment(), intensity: 1, showBackground: true })
            const maps = new Set()
            for (const name of ['venice_sunset', 'kloppenheim_06', 'studio_small_09']) {
              await r.loadEnvironment('/generated/' + name + '.bin')
              await r.drawFrame()
              maps.add(canvas.toDataURL())
              if (r.getAnimationState().time !== 0.5 || r.getAnimationState().playing)
                throw Error('Environment reset Fox animation state')
            }
            if (maps.size !== 3) throw Error('Fox environments did not change pixels')
            const foxError = await device.popErrorScope()
            if (foxError) throw Error(foxError.message)
            r.dispose()
            host.remove()
            return (
              'PASS GPU readback/native reference parity (max error ' +
              maximum +
              '), clip switch, skin pixels, playback/pause, invalid state and raster capability'
            )
          } finally {
            device.destroy()
            data.images.forEach((i) => i.bitmap?.close())
          }
        },
        {
          root: '/@fs' + resolve('web/src'),
          native,
          foxNative,
          probeURL: '/@fs' + resolve('scripts/webgpu/skin-readback.js'),
        },
      ),
    )
  }
  // The fifth example is checked separately once mounted.
  if (process.env.NOVA_ANIMATION_PAGE === '1' || process.env.NOVA_PREVIEW_URL) {
    await page.goto(base + '/examples/skeletal-animation')
    await page.locator('[data-ready="true"]').waitFor({ timeout: 60000 })
    const playing = page.getByRole('checkbox', { name: '播放动画', exact: true })
    await playing.scrollIntoViewIfNeeded()
    await playing.uncheck()
    const time = page.getByRole('slider', { name: '动画时间', exact: true })
    await time.fill('.5')
    await page.getByRole('combobox', { name: '动画片段', exact: true }).selectOption('2')
    await time.fill('.25')
    await page.waitForTimeout(100)
    const firstPose = await page.locator('canvas').evaluate((canvas) => canvas.toDataURL())
    await time.fill('.75')
    await page.waitForTimeout(100)
    const secondPose = await page.locator('canvas').evaluate((canvas) => canvas.toDataURL())
    if (firstPose === secondPose) throw Error('Static preview did not render GPU skin deformation')
    await time.fill('.5')
    await page.getByRole('slider', { name: '动画速度', exact: true }).fill('1.5')
    const mode = page.getByRole('combobox', { name: '渲染模式', exact: true })
    if (!(await mode.locator('option[value="path-tracing"]').isDisabled()))
      throw Error('Animated path mode enabled')
    await playing.check()
    await page.waitForTimeout(150)
    await playing.uncheck()
    await mkdir('build/validation', { recursive: true })
    await page.locator('.preview').screenshot({ path: 'build/validation/animation-web-panel.png' })
    const maps = page.getByRole('combobox', { name: '环境贴图', exact: true })
    if ((await maps.locator('option').count()) !== 3) throw Error('Fox environment options missing')
    await maps.selectOption('/generated/kloppenheim_06.bin')
    await page.waitForFunction(() => !document.querySelector('[aria-label="环境贴图"]').disabled)
    if ((await maps.inputValue()) !== '/generated/kloppenheim_06.bin')
      throw Error('Fox environment selection failed')
    await page.getByRole('checkbox', { name: '显示天空盒', exact: true }).uncheck()
    await page.getByRole('slider', { name: '环境强度', exact: true }).fill('.7')
    await page.getByRole('checkbox', { name: '显示天空盒', exact: true }).check()
    await playing.check()
    await page.getByRole('button', { name: '隐藏预览', exact: true }).click()
    await page.waitForTimeout(100)
    const hiddenTime = await page.locator('[aria-label="动画时间"]').inputValue()
    await page.waitForTimeout(250)
    if ((await page.locator('[aria-label="动画时间"]').inputValue()) !== hiddenTime)
      throw Error('Hidden animation advanced')
    await page.getByRole('button', { name: '显示预览', exact: true }).click()
    await page.waitForFunction(
      (old) => document.querySelector('[aria-label="动画时间"]').value !== old,
      hiddenTime,
      { timeout: 10000 },
    )
    await playing.uncheck()
    await page.setViewportSize({ width: 390, height: 900 })
    await page.goto(base + '/examples/skeletal-animation')
    await page.locator('[data-ready="true"]').waitFor({ timeout: 60000 })
    await page.getByRole('button', { name: '显示参数面板', exact: true }).click()
    await page.getByRole('combobox', { name: '动画片段', exact: true }).selectOption('1')
    await page.getByRole('checkbox', { name: '播放动画', exact: true }).uncheck()
    const panel = await page.locator('.nova-panel').boundingBox(),
      canvas = await page.locator('canvas').boundingBox()
    if (panel.x < canvas.x || panel.x + panel.width > canvas.x + canvas.width + 1)
      throw Error('Mobile animation panel overflow')

    console.log(
      'PASS fifth example clip selector, playback, speed, seek, capability and preview lifecycle',
    )
  }
  if (errors.length) throw Error(errors.join('\n'))
} finally {
  await browser.close()
}
