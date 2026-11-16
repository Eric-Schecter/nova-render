import { execFileSync } from 'node:child_process'
import { mkdtempSync, cpSync, readdirSync, renameSync, existsSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
const archive = resolve(process.argv[2]),
  api = process.argv[3] || 'metal',
  config = process.argv[4] || 'Release'
const work = mkdtempSync(join(tmpdir(), 'nova-render-sdk-consumer-')),
  cmake = process.env.CMAKE || 'cmake'
const run = (args) => execFileSync(cmake, args, { stdio: 'inherit', cwd: work })
run(['-E', 'tar', 'xf', archive])
const directory = readdirSync(work, { withFileTypes: true }).find((f) => f.isDirectory())
if (!directory) throw Error('SDK archive has no top-level directory')
const sdk = join(work, 'relocated SDK')
renameSync(join(work, directory.name), sdk)
const app = join(work, 'application')
cpSync(new URL('../../examples/sdk-consumer/', import.meta.url), app, { recursive: true })
run([
  '-S',
  app,
  '-B',
  join(work, 'build'),
  '-DCMAKE_BUILD_TYPE=' + config,
  '-DCMAKE_PREFIX_PATH=' + sdk,
  '-DNOVA_EXPECTED_SDK=' + sdk,
  '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF',
  '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF',
])
run(['--build', join(work, 'build'), '--config', config])
const candidates =
  process.platform === 'win32'
    ? [
        join(work, 'build', config, 'nova_sdk_consumer.exe'),
        join(work, 'build', 'nova_sdk_consumer.exe'),
      ]
    : [join(work, 'build', 'nova_sdk_consumer')]
const executable = candidates.find(existsSync)
if (!executable) throw Error('Consumer executable missing')
const models = join(work, 'models')
cpSync(new URL('../../assets/SceneSample/', import.meta.url), models, { recursive: true })
cpSync(new URL('../../assets/MaterialSample/Dual.glb', import.meta.url), join(models, 'Dual.glb'))
execFileSync(executable, [api, models], { stdio: 'inherit', cwd: work })
console.log('PASS relocated SDK consumed outside repository:', work)
