// Pair already-built native/npm archives; reject mixed source revisions and dirty snapshots.
import { execFileSync } from 'node:child_process'
import {
  readFileSync,
  writeFileSync,
  mkdtempSync,
  readdirSync,
  mkdirSync,
  cpSync,
  rmSync,
} from 'node:fs'
import { resolve, join, basename } from 'node:path'
import { tmpdir } from 'node:os'
import { createHash } from 'node:crypto'
const native = resolve(process.argv[2]),
  web = resolve(process.argv[3]),
  output = resolve(process.argv[4] || 'build/packages/paired')
const temporary = mkdtempSync(join(tmpdir(), 'nova-package-pair-'))
function find(dir, name) {
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const path = join(dir, entry.name)
    if (entry.isDirectory()) {
      const result = find(path, name)
      if (result) return result
    } else if (entry.name === name) return path
  }
}
try {
  mkdirSync(join(temporary, 'native'))
  mkdirSync(join(temporary, 'web'))
  execFileSync('cmake', ['-E', 'tar', 'xf', native], { cwd: join(temporary, 'native') })
  execFileSync('cmake', ['-E', 'tar', 'xf', web], { cwd: join(temporary, 'web') })
  const nativeInfo = JSON.parse(
      readFileSync(find(join(temporary, 'native'), 'build-info.json'), 'utf8'),
    ),
    webInfo = JSON.parse(readFileSync(join(temporary, 'web/package/assets/manifest.json'), 'utf8'))
  if (
    nativeInfo.sourceDirty ||
    webInfo.sourceDirty ||
    !nativeInfo.sourceCommit ||
    nativeInfo.sourceCommit !== webInfo.sourceCommit ||
    nativeInfo.version !== webInfo.version
  )
    throw Error('Native/Web archives must use the same clean source commit and version')
  const artifact = (file) => ({
    file: basename(file),
    sha256: createHash('sha256').update(readFileSync(file)).digest('hex'),
  })
  const manifest = {
    schemaVersion: 1,
    version: nativeInfo.version,
    sourceCommit: nativeInfo.sourceCommit,
    sourceDirty: false,
    features: nativeInfo.features,
    native: {
      ...artifact(native),
      platform: nativeInfo.platform,
      architecture: nativeInfo.architecture,
      configuration: nativeInfo.configuration,
      uiEnabled: nativeInfo.uiEnabled,
    },
    web: { ...artifact(web), packageName: '@nova/render-web' },
  }
  mkdirSync(output, { recursive: true })
  for (const file of [native, web])
    if (resolve(output, basename(file)) !== file) cpSync(file, join(output, basename(file)))
  writeFileSync(join(output, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n')
  writeFileSync(
    join(output, 'SHA256SUMS'),
    [manifest.native, manifest.web].map((f) => `${f.sha256}  ${f.file}`).join('\n') + '\n',
  )
  console.log('Paired SDK archives:', join(output, 'manifest.json'))
} finally {
  rmSync(temporary, { recursive: true, force: true })
}
