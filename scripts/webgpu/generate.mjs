import { readFile, mkdir, writeFile, rm } from 'node:fs/promises'
import { compile as compileShader } from './compiler.mjs'
// Repository kernels are larger than editable snippets; HTTP compilation keeps its 64 KiB limit.
const compile = (source, options = {}) =>
  compileShader(source, { ...options, maxSourceBytes: 262144 })
const sheenSource = await readFile('render/shaders/material-sheen.hlsli', 'utf8')
const filmSource = await readFile('render/shaders/material-iridescence.hlsli', 'utf8')
const anisotropySource = await readFile('render/shaders/material-anisotropy.hlsli', 'utf8')
const readMaterialShader = async (name) =>
  (await readFile(`render/shaders/${name}.hlsl`, 'utf8')).replace('#include "material-sheen.hlsli"', sheenSource)
    .replace('#include "material-iridescence.hlsli"', filmSource)
    .replace('#include "material-anisotropy.hlsli"', anisotropySource)
const result = await compile(await readFile('render/shaders/triangle.hlsl', 'utf8'))
await mkdir('docs/public/generated', { recursive: true })
await writeFile('docs/public/generated/triangle.json', JSON.stringify(result))
console.log('HLSL → SPIR-V → WGSL generated')

const modelSource = await readMaterialShader('model')
const disableSheen = '#define NOVA_DISABLE_SHEEN 1\n'
const model = await compile(modelSource, { model: true })
const modelNoSheen = await compile(disableSheen + modelSource, { model: true, optimizeModel: true })
await writeFile('docs/public/generated/model-no-sheen.json', JSON.stringify(modelNoSheen))
await writeFile('docs/public/generated/model.json', JSON.stringify(model))
await writeFile(
  'docs/public/generated/DamagedHelmet.glb',
  await readFile('assets/DamagedHelmet/DamagedHelmet.glb'),
)
console.log('Model HLSL and GLB generated for WebGPU')

const sky = await compile(await readMaterialShader('model'), {
  model: true,
  sky: true,
})
await writeFile('docs/public/generated/skybox.json', JSON.stringify(sky))
console.log('Skybox HLSL and prefiltered HDR environment generated')

for (const name of [
  'Avocado',
  'BoomBox',
  'CornellBox',
  'CornellIndirect',
  'CornellGlossy',
  'CornellCaustics',
  'WeekendSpheres',
  'SkinningRobot',
  'Fox',
]) {
  await writeFile(`docs/public/generated/${name}.glb`, await readFile(`assets/${name}/${name}.glb`))
}
// Standard binary MIME avoids on-demand text/Brotli compression in static preview.
for (const name of ['venice_sunset', 'kloppenheim_06', 'studio_small_09']) {
  await writeFile(
    `docs/public/generated/${name}.bin`,
    await readFile(`assets/Environment/${name}.nenv`),
  )
  await rm(`docs/public/generated/${name}.nenv`, { force: true })
}

const path = await compile(await readMaterialShader('pathtrace'), {
  compute: true,
})
await writeFile('docs/public/generated/pathtrace.json', JSON.stringify(path))
const present = await compile(await readFile('render/shaders/pathpresent.hlsl', 'utf8'))
await writeFile('docs/public/generated/pathpresent.json', JSON.stringify(present))
console.log('BVH path tracing HLSL → WGSL generated')

const denoise = await compile(await readMaterialShader('pathtrace'), {
  compute: true,
  computeEntry: 'DenoiseMain',
})
await writeFile('docs/public/generated/denoise.json', JSON.stringify(denoise))

const skinning = await compile(await readMaterialShader('model'), {
  compute: true,
  computeEntry: 'SkinningCSMain',
})
await writeFile('docs/public/generated/skinning.json', JSON.stringify(skinning))

for (const [name, entry] of [
  ['photon-clear', 'PhotonClear'],
  ['photon-trace', 'PhotonMain'],
  ['mlt-bootstrap', 'MLTBootstrap'],
  ['mlt-reduce', 'MLTReduce'],
  ['mlt-mutate', 'MLTMutate'],
]) {
  const shader = await compile(await readMaterialShader('pathtrace'), {
    compute: true,
    computeEntry: entry,
  })
  await writeFile(`docs/public/generated/${name}.json`, JSON.stringify(shader))
}

const aoSource = (await readFile('render/shaders/ambient-occlusion.hlsl', 'utf8')).replace(
  '#include "pathtrace.hlsl"',
  await readMaterialShader('pathtrace'),
)
for (const [name, entry] of [
  ['ao-prepare', 'AOPrepare'],
  ['ao-pyramid', 'AOPyramid'],
  ['ao-main', 'AOMain'],
  ['ao-filter', 'AOFilter'],
]) {
  const shader = await compile(aoSource, { compute: true, computeEntry: entry })
  await writeFile(`docs/public/generated/${name}.json`, JSON.stringify(shader))
}
await writeFile(
  'docs/public/generated/SponzaWhite.glb',
  await readFile('assets/SponzaWhite/SponzaWhite.glb'),
)

// Built-in AA library only: flatten vendored headers, preserve original notices on disk.
const aaSource = await readFile('render/shaders/antialiasing.hlsl', 'utf8')
for (let pass = 0; pass < 9; pass++) {
  let source = aaSource.replace(
    '#include "texture-filtering.hlsli"',
    await readFile('render/shaders/texture-filtering.hlsli', 'utf8'),
  )
  for (const [file, needed] of [
    ['third_party/fxaa/FXAA.hlsli', pass === 2],
    ['third_party/smaa/SMAA.hlsl', pass >= 4 && pass <= 6],
  ]) {
    const text = needed ? await readFile('render/shaders/' + file, 'utf8') : ''
    source = source.replace(
      '#include "' + file + '"',
      text.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, ''),
    )
  }
  const shader = await compile('#define AA_PASS ' + pass + '\n' + source)
  await writeFile('docs/public/generated/aa-' + pass + '.json', JSON.stringify(shader))
}
for (const [name, channels] of [
  ['Area', 2],
  ['Search', 1],
]) {
  const text = await readFile('assets/AntiAliasing/' + name + 'Tex.h', 'utf8')
  const bytes = [...text.matchAll(/0x([0-9a-f]{2})/gi)].map((m) => parseInt(m[1], 16))
  const rgba = new Uint8Array((bytes.length / channels) * 4)
  for (let i = 0; i < bytes.length / channels; i++) {
    for (let c = 0; c < channels; c++) rgba[i * 4 + c] = bytes[i * channels + c]
    rgba[i * 4 + 3] = 255
  }
  await writeFile('docs/public/generated/smaa-' + name.toLowerCase() + '.bin', rgba)
}
console.log('AA HLSL and SMAA reference tables generated')

const rasterSource =
  '#define NOVA_RASTER_AA 1\n' + (await readMaterialShader('model'))
for (const [name, vertexEntry, fragmentEntry] of [
  ['model-aa', 'VSMain', 'PSMain'],
  ['model-gbuffer', 'VSMain', 'GBufferPSMain'],
  ['model-oit', 'VSMain', 'OITPSMain'],
  ['model-deferred', 'SkyVSMain', 'DeferredPSMain'],
  ['model-hybrid-shadow', 'SkyVSMain', 'HybridShadowPSMain'],
  ['sky-aa', 'SkyVSMain', 'SkyPSMain'],
  ['model-motion', 'MotionVSMain', 'MotionPSMain'],
  ['sky-motion', 'SkyVSMain', 'SkyMotionPSMain'],
]) {
  // The Hybrid visibility kernel needs inlining to retain storage-matrix layout
  // through Naga's function-local struct lowering. Covered by native/Web ray parity.
  const data = await compile(rasterSource, {
    model: true,
    vertexEntry,
    fragmentEntry,
    optimizeModel: ['model-hybrid-shadow', 'model-deferred'].includes(name),
  })
  await writeFile('docs/public/generated/' + name + '.json', JSON.stringify(data))
  if (name === 'model-aa' || name === 'model-oit') {
    const variant = await compile(disableSheen + rasterSource, {
      model: true, vertexEntry, fragmentEntry, optimizeModel: true,
    })
    await writeFile('docs/public/generated/' + name + '-no-sheen.json', JSON.stringify(variant))
  }
}

const probeUpdate = await compile(rasterSource, {
  compute: true,
  computeEntry: 'HybridProbeUpdateCS',
  optimizeCompute: true,
})
const probeDeferred = await compile('#define NOVA_HYBRID_PROBES 1\n' + rasterSource, {
  model: true,
  vertexEntry: 'SkyVSMain',
  fragmentEntry: 'DeferredPSMain',
  optimizeModel: true,
})
await writeFile(
  'docs/public/generated/model-probes.json',
  JSON.stringify({
    compute: probeUpdate.compute,
    vert: probeDeferred.vert,
    frag: probeDeferred.frag,
  }),
)

for (const pass of [9, 10]) {
  const data = await compile(
    '#define AA_PASS ' + pass + '\n' + (await readFile('render/shaders/shadows.hlsl', 'utf8')),
  )
  await writeFile('docs/public/generated/aa-' + pass + '.json', JSON.stringify(data))
}
console.log('Shadow map and shadow scene HLSL generated')

for (let pass = 11; pass <= 16; pass++) {
  const data = await compile(
    '#define AA_PASS ' +
      pass +
      '\n' +
      (await readFile('render/shaders/postprocessing.hlsl', 'utf8')),
  )
  await writeFile('docs/public/generated/aa-' + pass + '.json', JSON.stringify(data))
}
console.log('HDR post-processing HLSL generated')

for (let pass = 17; pass <= 21; pass++) {
  const data = await compile(
    '#define AA_PASS ' +
      pass +
      '\n' +
      (await readFile('render/shaders/ssr.hlsl', 'utf8')).replace(
        '#include \"probe-scene.hlsli\"',
        await readFile('render/shaders/probe-scene.hlsli', 'utf8'),
      ),
  )
  await writeFile('docs/public/generated/aa-' + pass + '.json', JSON.stringify(data))
}
console.log('SSR surface, depth hierarchy, tracing and composition HLSL generated')

for (const color of ['Red', 'Blue']) {
  await writeFile(
    `docs/public/generated/Scene${color}.glb`,
    await readFile(`assets/SceneSample/${color}.glb`),
  )
}

await writeFile(
  'docs/public/generated/viewport-present.json',
  JSON.stringify(await compile(await readFile('render/shaders/viewport-present.hlsl', 'utf8'))),
)

await writeFile(
  'docs/public/generated/MaterialDual.glb',
  await readFile('assets/MaterialSample/Dual.glb'),
)

await import('./generate-pipelines.mjs')

await writeFile(
  'docs/public/generated/aa-29.json',
  JSON.stringify(await compile(await readFile('render/shaders/transparency.hlsl', 'utf8'))),
)

for (const name of ['Layers', 'LayersReverse', 'Mask', 'Quad', 'Back', 'Mirrored']) {
  await writeFile(
    `docs/public/generated/Material${name}.glb`,
    await readFile(`assets/MaterialSample/${name}.glb`),
  )
}
