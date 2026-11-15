import { chromium } from 'playwright'
import { realpathSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5197'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({
    viewport: { width: 800, height: 800 },
    deviceScaleFactor: 2,
  })
  await page.goto(base)
  const result = await page.evaluate(
    async (root) => {
      const { showHelmet } = await import(root + '/web/examples/helmet.js'),
        { showTriangle } = await import(root + '/web/examples/triangle.js'),
        { showAnimation } = await import(root + '/web/examples/animation.js'),
        { showComparison } = await import(root + '/web/examples/comparison.js')
      document.body.replaceChildren()
      const check = (value, message) => {
          if (!value) throw Error(message)
        },
        delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms))
      const canvas = () => {
        const c = document.createElement('canvas')
        c.style = 'width:301px;height:241px;display:block'
        document.body.append(c)
        return c
      }
      let c = canvas(),
        scene = await showTriangle(c, await (await fetch('/generated/triangle.json')).json())
      let stats = scene.getPerformanceStats()
      check(
        stats.frameCount === 1 && stats.fps === null && stats.state === 'idle',
        'First static frame is not idle',
      )
      check(
        stats.width === 602 && stats.height === 482 && stats.scale === 1 && stats.lastRenderMs > 0,
        'Invalid raster metrics',
      )
      await scene.drawFrame()
      await delay(300)
      check(scene.getPerformanceStats().fps === null, 'Static cadence did not settle')
      scene.dispose()
      check(!document.querySelector('[aria-label="渲染性能"]'), 'Triangle metric leak')
      c.remove()
      c = canvas()
      scene = await showHelmet(c, '/generated/DamagedHelmet.glb', '/generated/', {
        yaw: 20,
        pitch: 5,
        distance: 3.6,
        exposure: 1,
      })
      scene.setContinuousRendering(false)
      scene.setRayTracing({
        ...scene.getRayTracing(),
        resolutionScale: 0.25,
        samplesPerFrame: 2,
        maxBounces: 2,
      })
      scene.setRenderMode('ray-tracing')
      await scene.drawFrame()
      stats = scene.getPerformanceStats()
      check(
        stats.width === 151 &&
          stats.height === 121 &&
          stats.samples === 2 &&
          stats.scale === 0.25 &&
          stats.accumulatedFrames >= 2 &&
          stats.method === 'PT',
        'Ray tracing dimensions/counts incorrect',
      )
      scene.setRenderMode('raster')
      await scene.drawFrame()
      check(scene.getPerformanceStats().scale === 1, 'Mode change stale metrics')
      scene.dispose()
      c.remove()
      c = canvas()
      scene = await showAnimation(c, '/generated/Fox.glb', '/generated/', {
        yaw: 65,
        pitch: 10,
        distance: 3,
        exposure: 1,
      })
      await delay(700)
      stats = scene.getPerformanceStats()
      check(
        stats.frameCount > 2 && stats.fps > 0 && stats.p95Ms > 0,
        'Animation completion cadence missing',
      )
      scene.setContinuousRendering(false)
      await scene.waitIdle()
      await delay(300)
      const count = scene.getPerformanceStats().frameCount
      await delay(300)
      check(
        scene.getPerformanceStats().frameCount === count &&
          scene.getPerformanceStats().fps === null,
        'Stopped animation still updates metrics',
      )
      scene.dispose()
      c.remove()
      const host = document.createElement('div')
      host.style.width = '300px'
      document.body.append(host)
      scene = await showComparison(host, '/generated/CornellBox.glb', '/generated/', {
        yaw: 0,
        pitch: 0,
        distance: 3.6,
        exposure: 1,
      })
      scene.setRayTracing({
        ...scene.getRayTracing(),
        resolutionScale: 0.25,
        mltBootstrapSamples: 256,
        photonsPerIteration: 256,
      })
      scene.setBudget({ samples: 2, milliseconds: 0 })
      const timeout = performance.now() + 30000
      while (!scene.getProgress().every((p) => p.complete)) {
        if (performance.now() > timeout) throw Error('Comparison budget timeout')
        await delay(50)
      }
      stats = scene.getPerformanceStats()
      check(
        stats.fps === null && stats.state === 'idle' && stats.method === 'Comparison (4 views)',
        'Comparison budget metrics not idle',
      )
      const first = host.querySelector('canvas')
      check(
        stats.width === 2 * Math.round(first.width * 0.25) &&
          stats.height === 2 * Math.round(first.height * 0.25),
        'Comparison total internal dimensions incorrect',
      )
      await delay(300)
      check(
        scene.getPerformanceStats().frameCount === stats.frameCount,
        'Budget-completed comparison keeps recording',
      )
      scene.dispose()
      check(!document.querySelector('[aria-label="渲染性能"]'), 'Comparison metric leak')
      return 'raster/ray dimensions and scale, samples, actual animation cadence, on-demand idle, comparison budget freeze and disposal'
    },
    '/@fs' + realpathSync('.'),
  )
  console.log('PASS', result)
} finally {
  await browser.close()
}
