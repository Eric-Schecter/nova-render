import { chromium } from 'playwright'
import { resolve } from 'node:path'
import { writeFile } from 'node:fs/promises'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage()
  await page.goto('http://127.0.0.1:5173/')
  const results = await page.evaluate(
    async (root) => {
      const { loadGLB, sceneUniforms } = await import(root + '/ModelData.js'),
        { createPathView } = await import(root + '/PathView.js'),
        { defaultEnvironment } = await import(root + '/EnvironmentData.js')
      const device = await (await navigator.gpu.requestAdapter()).requestDevice(),
        results = []
      device.pushErrorScope('validation')
      try {
        for (const [name, exposure] of [
          ['CornellBox', 3],
          ['CornellIndirect', 12],
          ['CornellGlossy', 16],
        ]) {
          const data = await loadGLB('/generated/' + name + '.glb'),
            size = 48,
            camera = { yaw: 0, pitch: 0, distance: 3.6, exposure },
            light = { direction: [0, 1, 0], color: [1, 1, 1], intensity: 0 },
            env = { ...defaultEnvironment(), intensity: 0, showBackground: false },
            settings = {
              samplesPerFrame: 4,
              maxBounces: 5,
              resolutionScale: 1,
              denoise: false,
              lightSampling: true,
              integrator: 'path',
              photonsPerIteration: 8192,
              photonRadius: 0.16,
            }
          // Primary floor visibility is classified before measuring any integrator.
          // Requiring all pixel corners to hit the floor excludes silhouette mixing.
          const u = sceneUniforms(data, camera, 1, 'pbr', light, null, env),
            floorPixels = []
          const triangles = []
          for (let i = 0; i < data.vertices.length; i += 48) {
            const a = [...data.vertices.slice(i, i + 3)],
              e = [0, 1, 2].map((k) => data.vertices[i + 16 + k] - a[k]),
              f = [0, 1, 2].map((k) => data.vertices[i + 32 + k] - a[k]),
              t = [0, 1, 2].map((k) => u[16 + k] - a[k]),
              q = [t[1] * e[2] - t[2] * e[1], t[2] * e[0] - t[0] * e[2], t[0] * e[1] - t[1] * e[0]]
            triangles.push({ e, f, t, q, top: f.reduce((s, x, k) => s + x * q[k], 0) })
          }
          function primary(x, y) {
            const px = (x / size) * 2 - 1,
              py = (y / size) * 2 - 1,
              d = [0, 1, 2].map((k) => u[48 + k] + px * u[43] * u[40 + k] - py * u[47] * u[44 + k])
            let nearest = Infinity,
              id = -1
            for (let i = 0; i < triangles.length; i++) {
              const { e, f, t, q, top } = triangles[i],
                p = [
                  d[1] * f[2] - d[2] * f[1],
                  d[2] * f[0] - d[0] * f[2],
                  d[0] * f[1] - d[1] * f[0],
                ],
                det = e[0] * p[0] + e[1] * p[1] + e[2] * p[2]
              if (Math.abs(det) < 1e-10) continue
              const b = (t[0] * p[0] + t[1] * p[1] + t[2] * p[2]) / det,
                c = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) / det,
                dist = top / det
              if (b >= 0 && c >= 0 && b + c <= 1 && dist > 0.0005 && dist < nearest) {
                nearest = dist
                id = i
              }
            }
            return id
          }
          for (let y = 0; y < size; y++)
            for (let x = 0; x < size; x++)
              if (
                [
                  [0.5, 0.5],
                  [0.05, 0.05],
                  [0.95, 0.05],
                  [0.05, 0.95],
                  [0.95, 0.95],
                ].every(([a, b]) => {
                  const id = primary(x + a, y + b)
                  return id === 0 || id === 1
                })
              )
                floorPixels.push(y * size + x)
          if (!floorPixels.length) throw Error('Floor classification failed')
          let path = await createPathView(device, data, '/generated/', { linearOutput: true })
          try {
            async function render(integrator, spp, budget = 0) {
              path.reset()
              settings.integrator = integrator
              await device.queue.onSubmittedWorkDone()
              const start = performance.now()
              let samples = 0
              while (budget ? performance.now() - start < budget : samples < spp) {
                path.draw(null, camera, [0, 0, 0, 1], size, size, 'pbr', light, null, env, settings)
                samples += 4
                if (budget) await device.queue.onSubmittedWorkDone()
              }
              const image = await path.readPixels()
              const values = new Float32Array(image.pixels.buffer)
              if (values.some((x) => !Number.isFinite(x) || x < 0))
                throw Error('Invalid radiance ' + name)
              return { values, spp: samples, ms: performance.now() - start }
            }
            const firstHalf = await render('bidirectional', 16384)
            // Continue the stream: A - (A+B)/2 estimates the variance of the
            // combined reference from two disjoint 16384-sample blocks.
            for (let i = 0; i < 16384; i += 4)
              path.draw(null, camera, [0, 0, 0, 1], size, size, 'pbr', light, null, env, settings)
            const refImage = await path.readPixels()
            const reference = { values: new Float32Array(refImage.pixels.buffer) }
            const audit = await render('path', 16384)
            function metric(p, error = true) {
              let sum = 0,
                count = 0
              for (let y = 12; y < 43; y++)
                for (let x = 9; x < 38; x++) {
                  const i = y * size + x
                  sum += error ? (p[i] - reference.values[i]) ** 2 : p[i]
                  count++
                }
              return sum / count
            }
            function floorMetric(values, error = true) {
              return (
                floorPixels.reduce(
                  (sum, i) => sum + (error ? (values[i] - reference.values[i]) ** 2 : values[i]),
                  0,
                ) / floorPixels.length
              )
            }
            const records = [],
              mean = metric(reference.values, false)
            for (const integrator of ['path', 'bidirectional', 'photon-mapping']) {
              await render(integrator, 4)
              for (const spp of [8, 32, 128, 512, 2048]) {
                const r = await render(integrator, spp)
                records.push({
                  integrator,
                  spp: r.spp,
                  ms: r.ms,
                  mse: metric(r.values),
                  relativeMSE: metric(r.values) / (mean * mean),
                  floorMSE: floorMetric(r.values),
                  floorRelativeMSE:
                    floorMetric(r.values) / floorMetric(reference.values, false) ** 2,
                  mean: metric(r.values, false),
                  emittedPhotons:
                    integrator === 'photon-mapping'
                      ? (r.spp / settings.samplesPerFrame) * settings.photonsPerIteration
                      : 0,
                })
              }
              for (const budget of [200, 1000]) {
                const r = await render(integrator, 0, budget)
                records.push({
                  integrator,
                  budgetMs: budget,
                  spp: r.spp,
                  ms: r.ms,
                  emittedPhotons:
                    integrator === 'photon-mapping'
                      ? (r.spp / settings.samplesPerFrame) * settings.photonsPerIteration
                      : 0,
                  mse: metric(r.values),
                  relativeMSE: metric(r.values) / (mean * mean),
                  floorMSE: floorMetric(r.values),
                  floorRelativeMSE:
                    floorMetric(r.values) / floorMetric(reference.values, false) ** 2,
                })
              }
            }
            path.dispose()
            path = await createPathView(device, data, '/generated/')
            const images = {}
            for (const [label, integrator, spp] of [
              ['pt', 'path', 32],
              ['bdpt', 'bidirectional', 32],
              ['photon', 'photon-mapping', 32],
              ['reference', 'bidirectional', 4096],
            ]) {
              settings.integrator = integrator
              path.reset()
              for (let i = 0; i < spp; i += 4)
                path.draw(null, camera, [0, 0, 0, 1], 96, 96, 'pbr', light, null, env, settings)
              const { pixels } = await path.readPixels(),
                canvas = document.createElement('canvas')
              canvas.width = canvas.height = 96
              canvas
                .getContext('2d')
                .putImageData(new ImageData(new Uint8ClampedArray(pixels), 96, 96), 0, 0)
              images[label] = canvas.toDataURL().split(',')[1]
            }
            results.push({
              name,
              size,
              maxBounces: 5,
              photonSettings: {
                photonsPerIteration: settings.photonsPerIteration,
                initialRadius: settings.photonRadius,
                samplesPerFrame: settings.samplesPerFrame,
                exposure: camera.exposure,
              },
              reference: {
                integrator: 'bidirectional',
                spp: 32768,
                mean,
                auditPTMean: metric(audit.values, false),
                auditRelativeMSE: metric(audit.values) / (mean * mean),
                floorPixels: floorPixels.length,
                floorMean: floorMetric(reference.values, false),
                referenceVariance: metric(firstHalf.values),
                floorReferenceVariance: floorMetric(firstHalf.values),
                floorAuditRelativeMSE:
                  floorMetric(audit.values) / floorMetric(reference.values, false) ** 2,
              },
              records,
              images,
            })
          } finally {
            path.dispose()
            data.images.forEach((i) => i.bitmap?.close())
          }
        }
        const error = await device.popErrorScope()
        if (error) throw Error(error.message)
        return results
      } finally {
        device.destroy()
      }
    },
    '/@fs' + resolve('web/src'),
  )
  for (const result of results) {
    for (const [label, png] of Object.entries(result.images))
      await writeFile(`build/validation/${result.name}-${label}.png`, Buffer.from(png, 'base64'))
    delete result.images
    console.log(
      result.name,
      result.reference,
      result.records.filter((r) => r.spp === 32 || r.budgetMs),
    )
  }
  await writeFile('build/validation/integrator-benchmark.json', JSON.stringify(results, null, 2))
} finally {
  await browser.close()
}
