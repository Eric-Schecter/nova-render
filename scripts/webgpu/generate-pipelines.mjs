import { readFile, writeFile, mkdir } from 'node:fs/promises'
import { compile } from './compiler.mjs'
await mkdir('docs/public/generated', { recursive: true })
const source = await readFile('render/shaders/pipelines.hlsl', 'utf8')
for (let pass = 22; pass <= 28; ++pass) {
  const result = await compile(`#define AA_PASS ${pass}\n${source}`)
  await writeFile(`docs/public/generated/aa-${pass}.json`, JSON.stringify(result))
  console.log(`Raster pipeline pass ${pass}: HLSL → WGSL`)
}
