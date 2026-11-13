import { readFile, mkdir, writeFile } from 'node:fs/promises'
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
await writeFile(
  'docs/public/generated/venice_sunset.nenv',
  await readFile('assets/Environment/venice_sunset.nenv'),
)
console.log('Skybox HLSL and prefiltered HDR environment generated')
