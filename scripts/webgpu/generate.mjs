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
