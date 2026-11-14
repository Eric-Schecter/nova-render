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
    await page.goto(base + '/')
    console.log(
      await page.evaluate(
        async ({ root, native }) => {
          const { loadGLB } = await import(root + '/ModelData.js'),
            { createAnimationPlayer } = await import(root + '/AnimationData.js'),
            Nova = await import(root + '/Nova.js')
          const data = await loadGLB('/generated/SkinningRobot.glb')
          try {
            const player = createAnimationPlayer(data)
            if (player.clips.length !== 2) throw Error('Clips missing')
            player.set({ ...player.state, time: 0.5, playing: false })
            let maximum = 0
            for (let i = 0; i < native.length; i++)
              maximum = Math.max(maximum, Math.abs(data.vertices[i] - native[i]))
            if (data.vertices.length !== native.length || maximum > 3e-5)
              throw Error('Native/web pose mismatch ' + maximum)
            const snapshot = data.vertices.slice()
            player.update(0.5)
            if (data.vertices.some((v, i) => v !== snapshot[i])) throw Error('Paused pose advanced')
            player.set({ ...player.state, clip: 1 })
            if (!data.vertices.some((v, i) => Math.abs(v - snapshot[i]) > 0.01))
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
            r.setAnimationState({ ...r.getAnimationState(), time: 0.5 })
            await r.drawFrame()
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
            r.dispose()
            host.remove()
            return (
              'PASS native/web pose parity (max error ' +
              maximum +
              '), clip switch, skin pixels, playback/pause, invalid state and raster capability'
            )
          } finally {
            data.images.forEach((i) => i.bitmap?.close())
          }
        },
        { root: '/@fs' + resolve('web/src'), native },
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
    await page.getByRole('combobox', { name: '动画片段', exact: true }).selectOption('1')
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
    await page.getByRole('button', { name: '隐藏预览', exact: true }).click()
    await page.getByRole('button', { name: '显示预览', exact: true }).click()
    console.log(
      'PASS fifth example clip selector, playback, speed, seek, capability and preview lifecycle',
    )
  }
  if (errors.length) throw Error(errors.join('\n'))
} finally {
  await browser.close()
}
