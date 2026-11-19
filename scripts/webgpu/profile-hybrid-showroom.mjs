import { chromium } from 'playwright'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { compile } from './compiler.mjs'

const archive = process.argv[2]
if (!archive) throw Error('Usage: profile-hybrid-showroom.mjs showroom.zip')
const source = '#define NOVA_RASTER_AA 1\n' + (await readFile('render/shaders/model.hlsl', 'utf8'))
const shadow = await compile(source, {
  model: true,
  vertexEntry: 'SkyVSMain',
  fragmentEntry: 'HybridShadowPSMain',
  optimizeModel: true,
  maxSourceBytes: 262144,
})
function replace(source, from, to) {
  if (!source.includes(from)) throw Error('SDK profiling anchor not found: ' + from)
  return source.replace(from, to)
}
const split = process.argv.includes('--reflection-split')
const cheap = process.argv.includes('--cheap-hit')
if (cheap && !split) throw Error('--cheap-hit requires --reflection-split')
const shade = split
  ? await compile(
      cheap
        ? replace(
            source,
            'radiance = hybridHitRadiance(instance, hitTri, bary, origin + ray * distance, ray);',
            'radiance = float3(0.15, 0.2, 0.25) + 0.001 * (frac(distance) + bary.x + float((instance ^ hitTri) & 7u));',
          )
        : source,
      {
        model: true,
        vertexEntry: 'SkyVSMain',
        fragmentEntry: 'DeferredPSMain',
        optimizeModel: true,
        maxSourceBytes: 262144,
      },
    )
  : null
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({
    viewport: { width: 1600, height: 1000 },
    deviceScaleFactor: 1,
  })
  await page.route('**/sdk/generated/model-hybrid-shadow.json', (route) =>
    route.fulfill({ json: shadow }),
  )
  if (shade)
    await page.route('**/sdk/generated/model-deferred.json', (route) =>
      route.fulfill({ json: shade }),
    )
  await page.route('**/sdk/src/Nova.js', async (route) => {
    let body = await (await route.fetch()).text()
    body = replace(
      body,
      '  const device = await adapter.requestDevice({',
      `  if (!adapter.features.has('timestamp-query')) throw Error('WebGPU timestamp-query unavailable')
  window.hybridAdapter = {vendor: adapter.info.vendor, architecture: adapter.info.architecture}
  const device = await adapter.requestDevice({ requiredFeatures: ['timestamp-query'],`,
    )
    body = replace(
      body,
      'export async function createRenderer(',
      'async function createRendererOriginal(',
    )
    body += `
export async function createRenderer(options) {
  const raw = await createRendererOriginal(options)
  window.profile = { raw, paused: false, enabled: false }
  return new Proxy(raw, {get(target, key) {
    if (key === 'drawFrame') return () => profile.paused ? Promise.resolve('skipped') : raw.drawFrame()
    const value = Reflect.get(target, key, target)
    return typeof value === 'function' ? value.bind(target) : value
  }})
}
`
    await route.fulfill({ contentType: 'text/javascript', body })
  })
  await page.route('**/sdk/src/AntiAliasingView.js', async (route) => {
    let body = await (await route.fetch()).text()
    body = replace(
      body,
      '    function render(p, index, present = false) {',
      `
    const labels = []
    const count = (passes.length + 1) * 2
    const enabled = window.profile?.enabled
    const queries = enabled ? device.createQuerySet({type: 'timestamp', count}) : null
    function render(p, index, present = false) {
      labels[index] = present ? 'display' : ({'-4':'gbuffer','-7':'shadows','-5':'lighting','7':'temporal'})[p.program] ?? 'other'
`,
    )
    body = replace(
      body,
      '      const pass = encoder.beginRenderPass({\n        colorAttachments,',
      `
      const pass = encoder.beginRenderPass({
        ...(enabled ? {timestampWrites:{querySet:queries,beginningOfPassWriteIndex:index*2,endOfPassWriteIndex:index*2+1}} : {}),
        colorAttachments,`,
    )
    body = replace(
      body,
      '    device.queue.submit([encoder.finish()])\n    await device.queue.onSubmittedWorkDone()',
      `
    if (enabled) {
      const size = count * 8
      const resolved = device.createBuffer({size,usage:GPUBufferUsage.QUERY_RESOLVE|GPUBufferUsage.COPY_SRC})
      const read = device.createBuffer({size,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ})
      encoder.resolveQuerySet(queries,0,count,resolved,0)
      encoder.copyBufferToBuffer(resolved,0,read,0,size)
      window.profile.read = async () => {
        try {
          await read.mapAsync(GPUMapMode.READ)
          const values = new BigUint64Array(read.getMappedRange())
          const times = {}
          for (const [i,label] of labels.entries()) {
            const delta = Number(values[i*2+1]-values[i*2])/1e6
            if (delta < 0) throw Error('Invalid GPU timestamp')
            times[label] = (times[label] || 0) + delta
          }
          return times
        } finally {
          read.destroy()
          resolved.destroy()
          queries.destroy()
        }
      }
    }
    const command = encoder.finish()
    const started = performance.now()
    device.queue.submit([command])
    await device.queue.onSubmittedWorkDone()
    if (window.profile) window.profile.submissionToCompletion = performance.now() - started
`,
    )
    body = replace(
      body,
      "    const encoder = device.createCommandEncoder()\n    device.pushErrorScope('validation')",
      "    let encoder = device.createCommandEncoder()\n    device.pushErrorScope('validation')",
    )
    body = replace(
      body,
      '    passes.forEach((p, i) => render(p, i))',
      `
    if (enabled) {
      const submit = async () => {
        device.queue.submit([encoder.finish()])
        await device.queue.onSubmittedWorkDone()
        encoder = device.createCommandEncoder()
      }
      await submit()
      for (const [i, p] of passes.entries()) {
        render(p, i)
        await submit()
      }
    } else {
      passes.forEach((p, i) => render(p, i))
    }
`,
    )
    await route.fulfill({ contentType: 'text/javascript', body })
  })
  await page.goto(process.env.NOVA_EDITOR_URL || 'http://127.0.0.1:5237/')
  await page.locator('input[accept=".zip"]').setInputFiles(resolve(archive))
  await page.getByText('5 个模型实例', { exact: true }).waitFor({ timeout: 60000 })
  await page.getByRole('button', { name: '视图', exact: true }).click()
  await page.getByRole('menuitem', { name: '预览主相机', exact: true }).click()
  await page.evaluate(async () => {
    profile.raw.setContinuousRendering(false)
    profile.paused = true
    await profile.raw.waitIdle()
  })
  await page.getByRole('combobox', { name: '渲染模式' }).selectOption('hybrid')
  const result = await page.evaluate(async (split) => {
    const { raw } = profile
    const camera = raw.getViewCamera()
    const median = (values) => {
      values.sort((a, b) => a - b)
      return (values[values.length / 2 - 1] + values[values.length / 2]) / 2
    }
    const cases = {}
    const effects = split
      ? [
          ['full', true, true],
          ['noReflections', true, false],
        ]
      : [
          ['full', true, true],
          ['noAO', false, true],
          ['noReflections', true, false],
          ['neither', false, false],
        ]
    for (const moving of [false, true]) {
      const name = moving ? 'moving' : 'still'
      cases[name] = {}
      for (const [label, ao, reflections] of effects) {
        raw.setViewCamera(camera)
        raw.setHybrid({
          ambientOcclusion: ao,
          reflections,
          shadows: true,
          shadowSamples: 1,
          aoSamples: 1,
          reflectionSamples: 1,
          temporalReconstruction: true,
        })
        const run = async (serial, count) => {
          raw.setViewCamera(camera)
          raw.resetHybridHistory()
          for (let i = 0; i < 4; i++) await raw.drawFrame()
          const samples = []
          profile.enabled = serial
          try {
            for (let i = 0; i < count; i++) {
              if (moving)
                raw.setViewCamera({
                  position: camera.position.map((x, k) => x + (k === 0 ? 0.014 * i : 0)),
                })
              profile.read = null
              const start = performance.now()
              if ((await raw.drawFrame()) !== 'rendered') throw Error('Frame did not render')
              const wall = performance.now() - start
              if (!serial) {
                samples.push({ wall, submissionToCompletion: profile.submissionToCompletion })
                continue
              }
              if (!profile.read) throw Error('GPU data missing')
              const stages = await profile.read()
              const sum = Object.values(stages).reduce((a, b) => a + b, 0)
              if (
                !(
                  sum > 0 &&
                  stages.gbuffer > 0 &&
                  stages.lighting > 0 &&
                  stages.shadows > 0 &&
                  sum <= wall
                )
              )
                throw Error('Invalid serialized GPU timestamps')
              samples.push(stages)
            }
          } finally {
            profile.enabled = false
          }
          return Object.fromEntries(
            Object.keys(samples[0]).map((key) => [key, median(samples.map((s) => s[key]))]),
          )
        }
        cases[name][label] = { frame: await run(false, 16), serialGpu: await run(true, 8) }
      }
    }
    const rect = document.querySelector('canvas').getBoundingClientRect()
    return {
      adapter: hybridAdapter,
      canvas: [rect.width, rect.height],
      samplesPerCase: { frame: 16, serialGpu: 8 },
      cases,
    }
  }, split)
  const dir = resolve('build/validation/hybrid-showroom')
  await mkdir(dir, { recursive: true })
  await writeFile(
    resolve(
      dir,
      split ? (cheap ? 'reflection-cheap-hit.json' : 'reflection-full.json') : 'gpu-profile.json',
    ),
    JSON.stringify(result, null, 2) + '\n',
  )
  console.log(JSON.stringify(result, null, 2))
} finally {
  await browser.close()
}
