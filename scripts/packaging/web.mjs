import { runNpm } from './process.mjs'
import { execFileSync } from 'node:child_process'
import { mkdirSync, rmSync, cpSync, readdirSync, readFileSync, writeFileSync } from 'node:fs'
import { resolve, join } from 'node:path'
import { createHash } from 'node:crypto'
import { fileURLToPath } from 'node:url'
const root = fileURLToPath(new URL('../../', import.meta.url)),
  stage = join(root, 'build/web-package'),
  out = join(root, 'build/packages')
execFileSync(process.execPath, [join(root, 'scripts/webgpu/generate.mjs')], {
  cwd: root,
  stdio: 'inherit',
})
rmSync(stage, { recursive: true, force: true })
mkdirSync(join(stage, 'assets'), { recursive: true })
mkdirSync(out, { recursive: true })
cpSync(join(root, 'web/src'), join(stage, 'src'), { recursive: true })
cpSync(join(root, 'packages/web/bin'), join(stage, 'bin'), { recursive: true })
cpSync(join(root, 'packages/web/README.md'), join(stage, 'README.md'))
const version = readFileSync(join(root, 'CMakeLists.txt'), 'utf8').match(
  /project\(NovaRender VERSION ([\d.]+)/,
)[1]
const assets = readdirSync(join(root, 'docs/public/generated')).filter(
  (f) => f.endsWith('.json') || /^smaa-(area|search)\.bin$/.test(f),
)
for (const file of assets)
  cpSync(join(root, 'docs/public/generated', file), join(stage, 'assets', file))
mkdirSync(join(stage, 'licenses'), { recursive: true })
for (const [source, name] of [
  ['render/shaders/third_party/smaa/LICENSE.txt', 'SMAA-LICENSE.txt'],
  ['render/shaders/third_party/fxaa/NOTICE.txt', 'FXAA-NOTICE.txt'],
])
  cpSync(join(root, source), join(stage, 'licenses', name))
const sourceCommit = execFileSync('git', ['rev-parse', 'HEAD'], {
  cwd: root,
  encoding: 'utf8',
}).trim()
const sourceDirty = !!execFileSync('git', ['status', '--porcelain', '--untracked-files=no'], {
  cwd: root,
  encoding: 'utf8',
}).trim()
const manifest = {
  schemaVersion: 1,
  sourceCommit,
  sourceDirty,
  version,
  assets: Object.fromEntries(
    assets.map((name) => [
      name,
      createHash('sha256')
        .update(readFileSync(join(stage, 'assets', name)))
        .digest('hex'),
    ]),
  ),
}
writeFileSync(join(stage, 'assets/manifest.json'), JSON.stringify(manifest, null, 2) + '\n')
writeFileSync(
  join(stage, 'package.json'),
  JSON.stringify(
    {
      name: '@nova/render-web',
      version,
      private: true,
      type: 'module',
      license: 'UNLICENSED',
      description: 'Nova Render WebGPU renderer with precompiled shared HLSL shaders',
      exports: {
        '.': './src/Nova.js',
        './assets/*': './assets/*',
        './package.json': './package.json',
      },
      bin: { 'nova-web-assets': './bin/copy-assets.mjs' },
      files: ['src', 'assets', 'bin', 'licenses', 'README.md'],
      engines: { node: '>=20' },
    },
    null,
    2,
  ) + '\n',
)
runNpm(
  ['pack', '--ignore-scripts', '--cache', join(root, 'build/npm-cache'), '--pack-destination', out],
  { cwd: stage, stdio: 'inherit' },
)
const file = join(out, `nova-render-web-${version}.tgz`)
writeFileSync(
  file + '.sha256',
  createHash('sha256').update(readFileSync(file)).digest('hex') +
    '  ' +
    file.split(/[\\/]/).pop() +
    '\n',
)
console.log(file)
