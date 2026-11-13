import { mkdtemp, readFile, writeFile, rm, readdir, realpath } from 'node:fs/promises'
import { tmpdir, homedir } from 'node:os'
import { join, resolve } from 'node:path'
import { execFile } from 'node:child_process'
import { promisify } from 'node:util'
const exec = promisify(execFile)
export async function compile(source, { model = false } = {}) {
  if (typeof source !== 'string' || Buffer.byteLength(source) > 65536) throw Error('HLSL 限制为 64 KiB')
  if (/^\s*#\s*include/m.test(source)) throw Error('在线示例不支持 #include')
  let dxc = process.env.NOVA_DXC_EXECUTABLE || 'dxc'
  if (!process.env.NOVA_DXC_EXECUTABLE && process.platform === 'darwin') {
    const base = join(homedir(), 'VulkanSDK')
    const versions = (await readdir(base,{withFileTypes:true})).filter(e=>e.isDirectory() && /^\d/.test(e.name)).map(e=>e.name).sort((a,b)=>b.localeCompare(a,undefined,{numeric:true}))
    if (versions.length) dxc = join(base, versions[0], 'macOS/bin/dxc')
  }
  const dir = await realpath(await mkdtemp(join(tmpdir(), 'nova-webgpu-')))
  try {
    const hlsl = join(dir,'triangle.hlsl'); await writeFile(hlsl, source)
    const result = { source }
    for (const [stage, entry, profile] of [['vert','VSMain','vs_6_0'],['frag','PSMain','ps_6_0']]) {
      const spv=join(dir,stage+'.spv'), wgsl=join(dir,stage+'.wgsl')
      await exec(dxc,['-spirv','-fspv-target-env=vulkan1.0',...(model ? ['-DNOVA_WEB_OUTPUT=1'] : []),'-T',profile,'-E',entry,'-Fo',spv,hlsl],{timeout:15000,maxBuffer:262144})
      await exec(process.execPath,[resolve('node_modules/naga-wasi-cli/bin/naga.mjs'),'--keep-coordinate-space',stage+'.spv',stage+'.wgsl'],{cwd:dir,timeout:15000,maxBuffer:262144})
      result[stage] = await readFile(wgsl,'utf8')
    }
    return result
  } catch(e) { throw Error(e.stderr || e.message) }
  finally { await rm(dir,{recursive:true,force:true}) }
}
