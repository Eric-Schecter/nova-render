import { chromium } from 'playwright'
import { resolve } from 'node:path'
import { mkdir } from 'node:fs/promises'
const base = process.env.NOVA_PREVIEW_URL || 'http://127.0.0.1:5173'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1200, height: 900 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  if (!process.env.NOVA_PREVIEW_URL) {
    await page.goto(base + '/')
    console.log(
      await page.evaluate(
        async (moduleURL) => {
          const Nova = await import(moduleURL),
            host = document.createElement('div'),
            canvas = document.createElement('canvas')
          host.style = 'position:fixed;inset:0;z-index:9999;background:#111'
          canvas.style = 'width:400px;height:300px'
          host.append(canvas)
          document.body.append(host)
          const r = await Nova.createRenderer({ canvas })
          window.pathTest = r
          window.pathCanvas = canvas
          let rejected = false
          try {
            r.setRenderMode('path-tracing')
          } catch {
            rejected = true
          }
          if (!rejected || r.getRenderMode() !== 'raster')
            throw Error('Model-free path mode accepted')
          if (!r.getCapabilities().pathTracing || !r.getCapabilities().softwareRayTracing)
            throw Error('BVH capability missing')
          await r.loadModel('/generated/DamagedHelmet.glb')
          r.setCamera({ yaw: 20, pitch: 5 })
          r.setLighting('pbr')
          await r.loadEnvironment('/generated/venice_sunset.bin')
          r.setDirectionalLight({ direction: [-1, 1.6, 2], color: [1, 1, 1], intensity: 0 })
          canvas.inert = true // Explicit frames are deterministic; background refinement pauses.
          await r.drawFrame()
          const raster = canvas.toDataURL()
          r.setPathTracing({ samplesPerFrame: 2, maxBounces: 3, resolutionScale: 0.25 })
          r.setRenderMode('path-tracing')
          await r.drawFrame()
          if (r.getAccumulatedSamples() !== 2) throw Error('Initial sample count')
          await r.drawFrame()
          if (r.getAccumulatedSamples() !== 4) throw Error('Accumulation')
          const changes = [
            () => r.setCamera({ yaw: 21 }),
            () =>
              r.setDirectionalLight({ direction: [0, 1, 1], color: [1, 0.6, 0.2], intensity: 2 }),
            () => r.setEnvironment({ ...r.getEnvironment(), rotation: 30 }),
            () => r.setClearColor([0.1, 0.2, 0.3, 1]),
          ]
          for (const change of changes) {
            change()
            await r.drawFrame()
            if (r.getAccumulatedSamples() !== 2)
              throw Error('Parameter failed to reset accumulation')
            await r.drawFrame()
          }
          r.resetAccumulation()
          if (r.getAccumulatedSamples() !== 0) throw Error('Manual reset')
          const saved = r.getPathTracing()
          for (const settings of [
            { ...saved, samplesPerFrame: 0 },
            { ...saved, maxBounces: 9 },
            { ...saved, resolutionScale: NaN },
          ]) {
            let fail = false
            try {
              r.setPathTracing(settings)
            } catch {
              fail = true
            }
            if (!fail || JSON.stringify(r.getPathTracing()) !== JSON.stringify(saved))
              throw Error('Invalid settings changed state')
          }
          for (const model of ['DamagedHelmet', 'Avocado', 'BoomBox']) {
            await r.loadModel('/generated/' + model + '.glb')
            for (const environment of ['venice_sunset', 'kloppenheim_06', 'studio_small_09']) {
              await r.loadEnvironment('/generated/' + environment + '.bin')
              await r.drawFrame()
              if (r.getAccumulatedSamples() !== 2) throw Error('Asset switch failed to reset')
            }
          }
          await r.loadModel('/generated/DamagedHelmet.glb')
          await r.loadEnvironment('/generated/venice_sunset.bin')
          r.setCamera({ yaw: 20, pitch: 5 })
          r.setDirectionalLight({ direction: [-1, 1.6, 2], color: [1, 1, 1], intensity: 0 })
          r.setEnvironment({ ...r.getEnvironment(), rotation: 0 })
          r.setClearColor([0.025, 0.035, 0.055, 1])
          r.setRenderMode('raster')
          await r.drawFrame()
          if (canvas.toDataURL() !== raster) throw Error('Raster pixels not restored')
          r.setRenderMode('path-tracing')
          await r.drawFrame()
          const count = r.getAccumulatedSamples()
          await new Promise((resolve) => setTimeout(resolve, 150))
          if (r.getAccumulatedSamples() !== count) throw Error('Hidden preview kept tracing')
          canvas.inert = false
          await new Promise((resolve) => setTimeout(resolve, 500))
          if (r.getAccumulatedSamples() <= count) throw Error('Refinement did not resume')
          r.setRenderMode('raster')
          await r.waitIdle()
          r.dispose()
          host.remove()
          return 'PASS API parity, settings validation, progressive frames, parameter resets, 9 asset combinations, exact raster restoration, hidden pause/resume and dispose'
        },
        '/@fs' + resolve('web/src/Nova.js'),
      ),
    )
  }
  for (const slug of ['damaged-helmet', 'lighting', 'environment']) {
    await page.goto(base + '/examples/' + slug)
    await page.locator('[data-ready="true"]').waitFor({ timeout: 60000 })
    const panel = page.getByRole('region', { name: '参数面板', exact: true })
    await panel.scrollIntoViewIfNeeded()
    const mode = panel.getByRole('combobox', { name: '渲染模式', exact: true })
    await mode.selectOption('path-tracing')
    await page.getByRole('slider', { name: '渲染比例', exact: true }).fill('.25')
    await page.getByRole('slider', { name: '反弹次数', exact: true }).fill('2')
    const samples = page.locator('[aria-label="累计采样"]')
    await page.waitForFunction(
      () =>
        Number(document.querySelector('[aria-label="累计采样"]')?.textContent.split(':')[1]) >= 4,
      null,
      { timeout: 60000 },
    )
    await page.getByRole('button', { name: '重置累计', exact: true }).click()
    await mode.selectOption('raster')
    if (await samples.count()) throw Error('Path controls remain in raster mode')
    await mode.selectOption('path-tracing')
    await page.waitForFunction(
      () =>
        Number(document.querySelector('[aria-label="累计采样"]')?.textContent.split(':')[1]) >= 8,
      null,
      { timeout: 60000 },
    )
    if (slug === 'environment') {
      await mkdir('build/validation', { recursive: true })
      await page.locator('.preview').screenshot({ path: 'build/validation/web-path-panel.png' })
      await page.getByRole('button', { name: '隐藏预览', exact: true }).click()
      await page.waitForTimeout(200)
      const before = await samples.textContent()
      await page.waitForTimeout(500)
      if ((await samples.textContent()) !== before) throw Error('Collapsed preview kept sampling')
      await page.getByRole('button', { name: '显示预览', exact: true }).click()
      await page.waitForFunction(
        (old) => document.querySelector('[aria-label="累计采样"]').textContent !== old,
        before,
        { timeout: 60000 },
      )
    }
    await mode.selectOption('raster')
    console.log('PASS real mode panel, path controls, reset and progressive render:', slug)
  }
  if (errors.length) throw Error(errors.join('\n'))
  // Moving away disposes the renderer and cancels refinement.
  await page.goto(base + '/api/')
  await page.waitForTimeout(150)
  console.log(
    'PASS browser path tracing lifecycle' + (process.env.NOVA_PREVIEW_URL ? ' on static site' : ''),
  )
} finally {
  await browser.close()
}
