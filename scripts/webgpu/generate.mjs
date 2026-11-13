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

for (const name of ['Avocado', 'BoomBox']) {
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
