import { readFile, mkdir, writeFile } from 'node:fs/promises'
import { compile } from './compiler.mjs'
const result = await compile(await readFile('render/shaders/triangle.hlsl','utf8'))
await mkdir('docs/public/generated',{recursive:true})
await writeFile('docs/public/generated/triangle.json',JSON.stringify(result))
console.log('HLSL → SPIR-V → WGSL generated')
