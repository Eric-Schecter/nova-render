import { chromium } from 'playwright'
import { readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { compile } from './compiler.mjs'
const source = await readFile('render/shaders/pathtrace.hlsl', 'utf8')
const probe = await compile(
  source +
    `
[numthreads(1,1,1)] void GlassProbe(uint3 id:SV_DispatchThreadID){
 PathVertex p=(PathVertex)0; p.geometric=p.normal=float3(0,0,1);p.transmission=1;p.ior=1.5;p.roughness=.06;
 float3 v=id.x==0?float3(0,0,1):id.x==1?float3(0,0,-1):normalize(float3(.9,0,-.3));
 uint state=1234;float total=0;
 for(uint i=0;i<8192;i++){float3 l;float pdf;if(walkDirection(p,v,state,l,pdf))total+=pathF(p,v,l).x*abs(dot(p.geometric,l))/pdf;}
 outputImage[id.x]=asuint(total/8192);
}`,
  { compute: true, computeEntry: 'GlassProbe' },
)
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage()
  await page.goto('http://127.0.0.1:5173/')
  const result = await page.evaluate(
    async ({ root, probe }) => {
      const device = await (await navigator.gpu.requestAdapter()).requestDevice()
      device.pushErrorScope('validation')
      const module = device.createShaderModule({ code: probe.compute }),
        pipeline = await device.createComputePipelineAsync({
          layout: 'auto',
          compute: { module, entryPoint: 'GlassProbe' },
        })
      const output = device.createBuffer({
          size: 16,
          usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC,
        }),
        read = device.createBuffer({
          size: 16,
          usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
        })
      const group = device.createBindGroup({
        layout: pipeline.getBindGroupLayout(0),
        entries: [{ binding: 6, resource: { buffer: output } }],
      })
      const encoder = device.createCommandEncoder(),
        pass = encoder.beginComputePass()
      pass.setPipeline(pipeline)
      pass.setBindGroup(0, group)
      pass.dispatchWorkgroups(3)
      pass.end()
      encoder.copyBufferToBuffer(output, 0, read, 0, 16)
      device.queue.submit([encoder.finish()])
      await read.mapAsync(GPUMapMode.READ)
      const energy = Array.from(new Float32Array(read.getMappedRange()).slice(0, 3))
      read.unmap()
      output.destroy()
      read.destroy()
      for (const [i, expected] of [0.04 + 0.96 / 2.25, 0.04 + 0.96 * 2.25, 1].entries())
        if (Math.abs(energy[i] / expected - 1) > 0.025)
          throw Error('Dielectric energy/TIR ' + energy)
      const { loadGLB } = await import(root + '/ModelData.js'),
        { createPathView } = await import(root + '/PathView.js')
      const data = await loadGLB('/generated/CornellCaustics.glb'),
        view = await createPathView(device, data, '/generated/', { linearOutput: true })
      const camera = { yaw: 0, pitch: 0, distance: 3.6, exposure: 1 },
        light = { direction: [0, 1, 0], color: [1, 1, 1], intensity: 0 },
        env = { rotation: 0, intensity: 0, backgroundIntensity: 0, showBackground: false },
        settings = {
          samplesPerFrame: 4,
          maxBounces: 8,
          resolutionScale: 1,
          denoise: false,
          lightSampling: true,
          photonsPerIteration: 8192,
          photonRadius: 0.1,
        },
        records = [],
        images = {}
      try {
        for (const integrator of ['path', 'bidirectional', 'photon-mapping']) {
          view.reset()
          settings.integrator = integrator
          for (let i = 0; i < 4096; i += 4)
            view.draw(null, camera, [0, 0, 0, 1], 48, 48, 'pbr', light, null, env, settings)
          const pixels = (await view.readPixels()).pixels,
            values = new Float32Array(pixels.buffer)
          if (values.some((x) => !Number.isFinite(x) || x < 0))
            throw Error('Nonfinite glass radiance')
          let mean = 0
          for (let y = 12; y < 43; y++)
            for (let x = 9; x < 38; x++) mean += values[y * 48 + x] / 899
          records.push({ integrator, mean })
          if (integrator === 'photon-mapping') images.nativeParity = Array.from(values)
        }
      } finally {
        view.dispose()
        data.images.forEach((i) => i.bitmap?.close())
      }
      const error = await device.popErrorScope()
      device.destroy()
      if (error) throw Error(error.message)
      if (
        Math.abs(records[1].mean / records[0].mean - 1) > 0.15 ||
        Math.abs(records[2].mean / records[0].mean - 1) > 0.2
      )
        throw Error('Glass energy mismatch ' + JSON.stringify(records))
      return { energy, records, ...images }
    },
    { root: '/@fs' + resolve('web/src'), probe },
  )
  await writeFile('build/validation/glass-web.json', JSON.stringify(result, null, 2))
  console.log(
    'PASS dielectric energy, inside/outside, total internal reflection and Cornell PT/BDPT/photon',
    result.energy,
    result.records,
  )
} finally {
  await browser.close()
}
