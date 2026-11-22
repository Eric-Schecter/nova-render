import { mkdtemp, readFile, writeFile, rm, readdir, realpath } from 'node:fs/promises'
import { tmpdir, homedir } from 'node:os'
import { join, resolve } from 'node:path'
import { execFile } from 'node:child_process'
import { promisify } from 'node:util'
const exec = promisify(execFile)
// Scalarize model storage without loop transforms that produce WGSL pointer spills.
const modelWGSLPasses = [
  'wrap-opkill',
  'eliminate-dead-branches',
  'merge-return',
  'inline-entry-points-exhaustive',
  'eliminate-dead-functions',
  'eliminate-dead-code-aggressive',
  'private-to-local',
  'eliminate-local-single-block',
  'eliminate-local-single-store',
  'eliminate-dead-code-aggressive',
  'scalar-replacement=0',
  'convert-local-access-chains',
  'eliminate-local-single-block',
  'eliminate-local-single-store',
  'eliminate-dead-code-aggressive',
  'ssa-rewrite',
  'eliminate-dead-code-aggressive',
  'ccp',
  'eliminate-dead-code-aggressive',
  'eliminate-dead-branches',
  'combine-access-chains',
  'simplify-instructions',
  'scalar-replacement=0',
  'convert-local-access-chains',
  'eliminate-local-single-block',
  'eliminate-local-single-store',
  'eliminate-dead-code-aggressive',
  'ssa-rewrite',
  'eliminate-dead-code-aggressive',
  'vector-dce',
  'eliminate-dead-inserts',
  'eliminate-dead-branches',
  'simplify-instructions',
  'if-conversion',
  'copy-propagate-arrays',
  'reduce-load-size',
  'eliminate-dead-code-aggressive',
  'merge-blocks',
  'eliminate-dead-branches',
  'merge-blocks',
  'simplify-instructions',
]
export async function compile(
  source,
  {
    model = false,
    sky = false,
    compute = false,
    computeEntry = 'CSMain',
    optimizeCompute = false,
    optimizeModel = false,
    vertexEntry,
    fragmentEntry,
    maxSourceBytes = 65536,
  } = {},
) {
  if (typeof source !== 'string' || Buffer.byteLength(source) > maxSourceBytes)
    throw Error(`HLSL source is limited to ${maxSourceBytes / 1024} KiB`)
  if (/^\s*#\s*include/m.test(source)) throw Error('Online examples do not support #include')
  let dxc = process.env.NOVA_DXC_EXECUTABLE || 'dxc'
  if (!process.env.NOVA_DXC_EXECUTABLE && process.platform === 'darwin') {
    const base = join(homedir(), 'VulkanSDK')
    const versions = (await readdir(base, { withFileTypes: true }))
      .filter((e) => e.isDirectory() && /^\d/.test(e.name))
      .map((e) => e.name)
      .sort((a, b) => b.localeCompare(a, undefined, { numeric: true }))
    if (versions.length) dxc = join(base, versions[0], 'macOS/bin/dxc')
  }
  const dir = await realpath(await mkdtemp(join(tmpdir(), 'nova-webgpu-')))
  try {
    const hlsl = join(dir, 'triangle.hlsl')
    await writeFile(hlsl, source)
    const result = { source }
    for (const [stage, entry, profile] of compute
      ? [['compute', computeEntry, 'cs_6_0']]
      : [
          ['vert', vertexEntry || (sky ? 'SkyVSMain' : 'VSMain'), 'vs_6_0'],
          ['frag', fragmentEntry || (sky ? 'SkyPSMain' : 'PSMain'), 'ps_6_0'],
        ]) {
      const spv = join(dir, stage + '.spv'),
        wgsl = join(dir, stage + '.wgsl')
      const optimizedModelFragment = model && optimizeModel && stage === 'frag'
      await exec(
        dxc,
        [
          '-spirv',
          '-fspv-target-env=vulkan1.0',
          ...(model ? ['-DNOVA_WEB_OUTPUT=1'] : []),
          // DXC LICM hoists storage pointers across nested MIS/BVH loops.
          // Naga cannot spill those pointers into WGSL function variables.
          ...(compute ? ['-DNOVA_WEB_BVH=1'] : []),
          // Inline storage accesses without LICM's non-storable pointer spills.
          ...(optimizedModelFragment
            ? ['-Oconfig=' + modelWGSLPasses.map((pass) => '--' + pass).join(',')]
            : []),
          ...((compute && !optimizeCompute) || (model && stage === 'frag' && !sky && !optimizeModel)
            ? ['-Od']
            : []),
          ...(['DenoiseMain', 'PhotonClear', 'MLTReduce', 'AOPyramid', 'AOFilter'].includes(
            computeEntry,
          )
            ? ['-DNOVA_DENOISE_ONLY=1']
            : []),
          '-T',
          profile,
          '-E',
          entry,
          '-Fo',
          spv,
          hlsl,
        ],
        { timeout: optimizedModelFragment ? 60000 : 15000, maxBuffer: 262144 },
      )
      await exec(
        process.execPath,
        [
          resolve('node_modules/naga-wasi-cli/bin/naga.mjs'),
          '--keep-coordinate-space',
          stage + '.spv',
          stage + '.wgsl',
        ],
        { cwd: dir, timeout: 15000, maxBuffer: 262144 },
      )
      result[stage] = await readFile(wgsl, 'utf8')
    }
    return result
  } catch (e) {
    throw Error(e.stderr || e.message)
  } finally {
    await rm(dir, { recursive: true, force: true })
  }
}
