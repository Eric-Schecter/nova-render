import { readFile, mkdir, writeFile, rm } from 'node:fs/promises'
import { compile } from './compiler.mjs'
const result = await compile(await readFile('render/shaders/triangle.hlsl', 'utf8'))
await mkdir('docs/public/generated', { recursive: true })
await writeFile('docs/public/generated/triangle.json', JSON.stringify(result))
console.log('HLSL → SPIR-V → WGSL generated')

const model = await compile(await readFile('render/shaders/model.hlsl', 'utf8'), { model: true })
await writeFile('docs/public/generated/model.json', JSON.stringify(model))
await writeFile(
  'docs/public/generated/DamagedHelmet.glb',
  await readFile('assets/DamagedHelmet/DamagedHelmet.glb'),
)
console.log('Model HLSL and GLB generated for WebGPU')

const sky = await compile(await readFile('render/shaders/model.hlsl', 'utf8'), {
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

const path = await compile(await readFile('render/shaders/pathtrace.hlsl', 'utf8'), {
  compute: true,
})
await writeFile('docs/public/generated/pathtrace.json', JSON.stringify(path))
const present = await compile(await readFile('render/shaders/pathpresent.hlsl', 'utf8'))
await writeFile('docs/public/generated/pathpresent.json', JSON.stringify(present))
console.log('BVH path tracing HLSL → WGSL generated')

const denoise = await compile(await readFile('render/shaders/pathtrace.hlsl', 'utf8'), {
  compute: true,
  computeEntry: 'DenoiseMain',
})
await writeFile('docs/public/generated/denoise.json', JSON.stringify(denoise))

const skinning = await compile(await readFile('render/shaders/model.hlsl', 'utf8'), {
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
  const shader = await compile(await readFile('render/shaders/pathtrace.hlsl', 'utf8'), {
    compute: true,
    computeEntry: entry,
  })
  await writeFile(`docs/public/generated/${name}.json`, JSON.stringify(shader))
}

const aoSource = (await readFile('render/shaders/ambient-occlusion.hlsl', 'utf8')).replace(
  '#include "pathtrace.hlsl"',
  await readFile('render/shaders/pathtrace.hlsl', 'utf8'),
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
  '#define NOVA_RASTER_AA 1\n' + (await readFile('render/shaders/model.hlsl', 'utf8'))
for (const [name, vertexEntry, fragmentEntry] of [
  ['model-aa', 'VSMain', 'PSMain'],
  ['sky-aa', 'SkyVSMain', 'SkyPSMain'],
  ['model-motion', 'MotionVSMain', 'MotionPSMain'],
  ['sky-motion', 'SkyVSMain', 'SkyMotionPSMain'],
]) {
  const data = await compile(rasterSource, { model: true, vertexEntry, fragmentEntry })
  await writeFile('docs/public/generated/' + name + '.json', JSON.stringify(data))
}

for (const pass of [9, 10]) {
  const data = await compile(
    '#define AA_PASS ' + pass + '\n' + (await readFile('render/shaders/shadows.hlsl', 'utf8')),
  )
  await writeFile('docs/public/generated/aa-' + pass + '.json', JSON.stringify(data))
}
console.log('Shadow map and shadow scene HLSL generated')
