import { chromium } from 'playwright'
import { mkdir, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'

const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5173'
const output = resolve('build/validation/path-clearcoat')
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage()
  await page.route('**/ClearCoatTest.glb', (route) =>
    route.fulfill({
      path: resolve('assets/ClearCoatTest/ClearCoatTest.glb'),
      contentType: 'model/gltf-binary',
    }),
  )
  await page.goto(base)
  const results = await page.evaluate(
    async (root) => {
      const { loadGLB } = await import(root + '/ModelData.js')
      const { packMaterials } = await import(root + '/MaterialData.js')
      const { createPathView } = await import(root + '/PathView.js')
      const { defaultEnvironment, loadEnvironmentFile } = await import(root + '/EnvironmentData.js')
      const device = await (await navigator.gpu.requestAdapter()).requestDevice()
      const source = await loadGLB('/ClearCoatTest.glb')
      const hdr = await loadEnvironmentFile('/generated/studio_small_09.bin')
      const maps = ['clearcoatTexture', 'clearcoatRoughnessTexture', 'clearcoatNormalTexture']
      const names = ['all', 'no-strength', 'no-roughness', 'no-normal']
      const camera = { yaw: 0, pitch: 0, distance: 2.62, exposure: 1 }
      const light = { direction: [0, 1, 2], color: [1, 1, 1], intensity: 0 }
      const environment = { ...defaultEnvironment(), showBackground: false }
      const options = { samplesPerFrame: 4, maxBounces: 2, resolutionScale: 1, lightSampling: true }
      const results = []
      let reference
      try {
        for (const [i, name] of names.entries()) {
          const materials = source.materials.map((m) => {
            // One reference tile is BLEND. Keep the ray mode's opaque contract.
            const material = { ...m, alphaMode: 'OPAQUE' }
            if (i) material[maps[i - 1]] = null
            return material
          })
          if (i && !source.materials.some((m) => m[maps[i - 1]]))
            throw Error('Missing source clearcoat map ' + maps[i - 1])
          device.pushErrorScope('validation')
          const path = await createPathView(
            device,
            packMaterials({ ...source, materials }),
            '/generated/',
          )
          try {
            for (let frame = 0; frame < 17; ++frame)
              path.draw(
                null,
                camera,
                [0.09, 0.09, 0.09, 1],
                256,
                256,
                'pbr',
                light,
                hdr,
                environment,
                options,
              )
            const image = await path.readPixels()
            if (path.samples !== 68) throw Error('Path accumulator did not reach 68 samples')
            const error = await device.popErrorScope()
            if (error) throw Error(error.message)
            const difference = i
              ? image.pixels.reduce((sum, value, j) => sum + Math.abs(value - reference[j]), 0)
              : 0
            if (!i) reference = image.pixels
            if (i && difference < 1000)
              throw Error('Path tracing did not sample map ' + maps[i - 1])
            const canvas = document.createElement('canvas')
            canvas.width = canvas.height = 256
            canvas
              .getContext('2d')
              .putImageData(new ImageData(new Uint8ClampedArray(image.pixels), 256, 256), 0, 0)
            results.push({ name, difference, png: canvas.toDataURL().split(',')[1] })
          } finally {
            path.dispose()
          }
        }
      } finally {
        device.destroy()
      }
      return results
    },
    '/@fs' + resolve('web/src'),
  )
  await mkdir(output, { recursive: true })
  for (const { name, difference, png } of results) {
    await writeFile(resolve(output, 'web-' + name + '.png'), Buffer.from(png, 'base64'))
    if (name !== 'all') console.log('ClearCoatTest', name, 'pixel delta', difference)
  }
} finally {
  await browser.close()
}
