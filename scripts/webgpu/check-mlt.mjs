import { chromium } from 'playwright'
import { readFile, writeFile, mkdir, copyFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { compile } from './compiler.mjs'
const original = await readFile('render/shaders/pathtrace.hlsl', 'utf8'),
  start = original.indexOf('float3 mltEvaluate('),
  end = original.indexOf('float mltIntensity(', start)
const analytic =
  original.slice(0, start) +
  `float3 mltEvaluate(out uint pixel){
 mltDimension=0;uint state=0;float2 uv=float2(random(state),random(state));uint2 size=asuint(parameters[24]).yz;
 uint2 p=min(uint2(uv*float2(size)),size-1);pixel=p.y*size.x+p.x;
 return float3(uv.x<.25?2:.25,.25+.5*uv.y,.8);
}\n` +
  original.slice(end)
const cdfProbe = await compile(
  original +
    `
[numthreads(1,1,1)] void MLTCDFProbe(uint3 id:SV_DispatchThreadID){outputImage[id.x]=mltSelectCDF(id.x==4?.999:float(id.x)*.25);}
`,
  { compute: true, maxSourceBytes: 128 * 1024, computeEntry: 'MLTCDFProbe' },
)
const folder = 'build/validation/mlt-analytic'
await mkdir(folder, { recursive: true })
for (const [name, entry] of [
  ['mlt-bootstrap', 'MLTBootstrap'],
  ['mlt-reduce', 'MLTReduce'],
  ['mlt-mutate', 'MLTMutate'],
])
  await writeFile(
    folder + '/' + name + '.json',
    JSON.stringify(
      await compile(analytic, { compute: true, maxSourceBytes: 128 * 1024, computeEntry: entry }),
    ),
  )
for (const name of [
  'pathtrace',
  'pathpresent',
  'denoise',
  'photon-clear',
  'photon-trace',
  'ao-prepare',
  'ao-pyramid',
  'ao-main',
  'ao-filter',
])
  await copyFile('docs/public/generated/' + name + '.json', folder + '/' + name + '.json')
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage()
  await page.goto('http://127.0.0.1:5173/')
  const result = await page.evaluate(
    async ({ root, analyticDirectory, cdfProbe }) => {
      const { createPathView, defaultPathTracing } = await import(root + '/PathView.js'),
        { emptyModelData, loadGLB } = await import(root + '/ModelData.js')
      const device = await (await navigator.gpu.requestAdapter()).requestDevice()
      device.pushErrorScope('validation')
      const camera = { yaw: 0, pitch: 0, distance: 3.6, exposure: 3 },
        light = { direction: [0, 1, 0], color: [1, 1, 1], intensity: 0 },
        env = { rotation: 0, intensity: 0, backgroundIntensity: 0, showBackground: false }
      const settings = {
        ...defaultPathTracing(),
        samplesPerFrame: 1,
        maxBounces: 8,
        resolutionScale: 1,
        mltBootstrapSamples: 4096,
        integrator: 'mlt',
      }
      const cdfParams = new Float32Array(388),
        cdfWords = new Uint32Array(cdfParams.buffer),
        cdfWork = new Uint32Array(600),
        cdfFloats = new Float32Array(cdfWork.buffer)
      cdfWords.set([0, 1, 1, 8], 96)
      cdfWords[107] = 256
      for (let i = 0; i < 256; i++) cdfFloats[31 + i * 2] = (i + 1) / 256
      const cdfResources = []
      function cdfBuffer(bytes, usage) {
        const b = device.createBuffer({
          size: bytes.byteLength,
          usage: usage | GPUBufferUsage.COPY_DST,
        })
        device.queue.writeBuffer(b, 0, bytes)
        cdfResources.push(b)
        return b
      }
      const cdfInput = cdfBuffer(cdfParams, GPUBufferUsage.STORAGE),
        cdfScratch = cdfBuffer(cdfWork, GPUBufferUsage.STORAGE),
        cdfOutput = cdfBuffer(new Uint32Array(8), GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC),
        cdfRead = device.createBuffer({
          size: 32,
          usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
        })
      cdfResources.push(cdfRead)
      const cdfPipeline = await device.createComputePipelineAsync({
          layout: 'auto',
          compute: {
            module: device.createShaderModule({ code: cdfProbe.compute }),
            entryPoint: 'MLTCDFProbe',
          },
        }),
        cdfGroup = device.createBindGroup({
          layout: cdfPipeline.getBindGroupLayout(0),
          entries: [
            { binding: 1, resource: { buffer: cdfInput } },
            { binding: 6, resource: { buffer: cdfOutput } },
            { binding: 7, resource: { buffer: cdfScratch } },
          ],
        })
      const cdfEncoder = device.createCommandEncoder(),
        cdfPass = cdfEncoder.beginComputePass()
      cdfPass.setPipeline(cdfPipeline)
      cdfPass.setBindGroup(0, cdfGroup)
      cdfPass.dispatchWorkgroups(5)
      cdfPass.end()
      cdfEncoder.copyBufferToBuffer(cdfOutput, 0, cdfRead, 0, 32)
      device.queue.submit([cdfEncoder.finish()])
      await cdfRead.mapAsync(GPUMapMode.READ)
      const indices = Array.from(new Uint32Array(cdfRead.getMappedRange()).slice(0, 5))
      cdfRead.unmap()
      cdfResources.forEach((b) => b.destroy())
      if (indices.some((x, i) => x !== [0, 64, 128, 192, 255][i]))
        throw Error('Bootstrap CDF buffer/index regression ' + indices)
      let view, data, firstAnalytic
      const records = [{ case: 'bootstrap-cdf-indices', indices }]
      async function image(integrator, spp, size, extra = {}) {
        view.reset()
        Object.assign(settings, { integrator }, extra)
        for (let i = 0; i < spp; i += settings.samplesPerFrame)
          view.draw(null, camera, [0, 0, 0, 1], size, size, 'pbr', light, null, env, settings)
        const pixels = (await view.readPixels()).pixels,
          values = new Float32Array(pixels.buffer)
        if (values.some((x) => !Number.isFinite(x) || x < 0)) throw Error('Nonfinite MLT radiance')
        return Array.from(values)
      }
      try {
        data = emptyModelData()
        data.vertices = new Float32Array([
          -1, -1, 10, 1, 0, 0, -1, 0, 0, 0, 0, 1.5, 0, 0, 0, 0, 1, -1, 10, 1, 0, 0, -1, 0, 0, 0, 0,
          1.5, 0, 0, 0, 0, 0, 1, 10, 1, 0, 0, -1, 0, 0, 0, 0, 1.5, 0, 0, 0, 0,
        ])
        view = await createPathView(device, data, analyticDirectory, { linearOutput: true })
        for (const probability of [0.3, 1]) {
          let firstError
          for (const mpp of [256, 2048]) {
            const values = await image('mlt', mpp, 16, { mltLargeStepProbability: probability }),
              stats = await view.readMLTStatistics()
            let mse = 0,
              mean = 0
            for (let y = 0; y < 16; y++)
              for (let x = 0; x < 16; x++) {
                const expected =
                  0.2126 * (x < 4 ? 2 : 0.25) +
                  0.7152 * (0.25 + (0.5 * (y + 0.5)) / 16) +
                  0.0722 * 0.8
                mse += (values[y * 16 + x] - expected) ** 2 / 256
                mean += values[y * 16 + x] / 256
              }
            const exact = 0.2126 * 0.6875 + 0.7152 * 0.5 + 0.0722 * 0.8
            if (
              stats.dimensionOverflow ||
              stats.proposals !== 256 * mpp ||
              stats.accepted <= 0 ||
              stats.accepted >= stats.proposals
            )
              throw Error('Invalid chain statistics ' + JSON.stringify(stats))
            if (Math.abs(stats.normalization - exact) > 5 * stats.normalizationSE + 0.001)
              throw Error('Bootstrap normalization failed')
            if (Math.abs(mean - stats.normalization) > 0.00005)
              throw Error('MLT film energy conservation failed ' + mean + ' ' + stats.normalization)
            if (firstError === undefined) firstError = mse
            else if (mse >= firstError * 0.8)
              throw Error('Analytic chain failed to converge ' + firstError + ' ' + mse)
            records.push({ case: 'analytic', probability, mpp, mse, mean, stats })
            if (probability === 0.3 && mpp === 256) firstAnalytic = values
          }
        }
        const batched = await image('mlt', 256, 16, {
          samplesPerFrame: 4,
          mltLargeStepProbability: 0.3,
        })
        if (batched.some((x, i) => Math.abs(x - firstAnalytic[i]) > 0.000002))
          throw Error('MLT batch invariance failed')
        records.push({ case: 'batch-invariance', passed: true })
        view.dispose()
        data = await loadGLB('/generated/CornellCaustics.glb')
        view = await createPathView(device, data, '/generated/', { linearOutput: true })
        settings.mltLargeStepProbability = 0.3
        settings.mltBootstrapSamples = 262144
        settings.samplesPerFrame = 4
        const reference = await image('bidirectional', 8192, 32),
          direct = await image('path', 2048, 32, { maxBounces: 1 })
        settings.maxBounces = 8
        settings.samplesPerFrame = 1
        let previous
        for (const mpp of [32, 256, 2048]) {
          const values = await image('mlt', mpp, 32),
            stats = await view.readMLTStatistics()
          let mean = 0,
            refMean = 0,
            mse = 0,
            all = 0,
            directMean = 0,
            refAll = 0
          for (let i = 0; i < values.length; i++) {
            all += values[i] / values.length
            directMean += direct[i] / values.length
            refAll += reference[i] / values.length
          }
          for (let y = 8; y < 29; y++)
            for (let x = 6; x < 26; x++) {
              const i = y * 32 + x
              mean += values[i] / 420
              refMean += reference[i] / 420
              mse += (values[i] - reference[i]) ** 2 / 420
            }
          if (stats.dimensionOverflow || stats.proposals !== 1024 * mpp)
            throw Error('Glass dimension or proposal count failed')
          if (Math.abs(all - directMean - stats.normalization) > 5 * stats.normalizationSE + 0.001)
            throw Error('Glass normalization mismatch')
          if (mpp === 2048 && Math.abs(mean / refMean - 1) > 0.15)
            throw Error(
              'Glass MLT energy mismatch ' +
                JSON.stringify({ mean, refMean, all, refAll, directMean, stats, records }),
            )
          records.push({ case: 'glass', mpp, mean, refMean, mse, all, refAll, directMean, stats })
          if (mpp === 32) previous = mse
          if (mpp === 2048 && mse >= previous) throw Error('Glass MLT did not converge')
          if (mpp === 2048) records.push({ case: 'native-parity', pixels: values })
        }
        const expectedIndirect = reference.reduce(
          (sum, x, i) => sum + (x - direct[i]) / reference.length,
          0,
        )
        for (const seed of [2, 3]) {
          settings.mltSeed = seed
          view.draw(null, camera, [0, 0, 0, 1], 32, 32, 'pbr', light, null, env, settings)
          const stats = await view.readMLTStatistics()
          if (view.samples !== 1) throw Error('MLT seed did not reset')
          if (Math.abs(stats.normalization - expectedIndirect) > 5 * stats.normalizationSE + 0.0002)
            throw Error('Independent bootstrap audit failed')
          records.push({ case: 'bootstrap-audit', seed, expectedIndirect, stats })
        }
        env.intensity = 1
        light.intensity = 3
        settings.mltBootstrapSamples = 4096
        view.draw(null, camera, [0, 0, 0, 1], 32, 32, 'pbr', light, null, env, settings)
        const mixed = await view.readMLTStatistics()
        if (mixed.dimensionOverflow) throw Error('Mixed-light primary dimension overflow')
        records.push({ case: 'mixed-light-dimensions', stats: mixed })
      } finally {
        view?.dispose()
        data?.images.forEach((i) => i.bitmap?.close())
      }
      const error = await device.popErrorScope()
      device.destroy()
      if (error) throw Error(error.message)
      return records
    },
    {
      root: '/@fs' + resolve('web/src'),
      analyticDirectory: '/@fs' + resolve(folder) + '/',
      cdfProbe,
    },
  )
  try {
    const raw = await readFile('build/validation/mlt-native.bin'),
      native = new Float32Array(raw.buffer, raw.byteOffset, raw.byteLength / 4),
      web = result.find((r) => r.case === 'native-parity').pixels
    if (native.length !== web.length) throw Error('Native MLT resolution mismatch')
    const meanNative = native.reduce((a, b) => a + b, 0) / native.length,
      meanWeb = web.reduce((a, b) => a + b, 0) / web.length
    if (Math.abs(meanNative / meanWeb - 1) > 0.02) throw Error('Native/WebGPU MLT energy mismatch')
    result.push({
      case: 'native-energy-parity',
      meanNative,
      meanWeb,
      relativeDifference: Math.abs(meanNative / meanWeb - 1),
    })
  } catch (error) {
    if (error.code !== 'ENOENT') throw error
  }
  await writeFile('build/validation/mlt-web.json', JSON.stringify(result, null, 2))
  console.log(
    'PASS analytic PSSMLT normalization, convergence, chain statistics, glass caustics and reset',
    result.filter((r) => r.case !== 'native-parity'),
  )
} finally {
  await browser.close()
}
