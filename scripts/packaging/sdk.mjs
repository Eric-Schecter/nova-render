import { execFileSync } from 'node:child_process'
import { resolve, join } from 'node:path'
import { mkdirSync, readdirSync, readFileSync, writeFileSync } from 'node:fs'
import { createHash } from 'node:crypto'
const args = process.argv.slice(2),
  get = (key, fallback) => {
    const i = args.indexOf(key)
    return i < 0 ? fallback : args[i + 1]
  }
const build = resolve(get('--build-dir', 'build/sdk')),
  config = get('--config', 'Release'),
  out = resolve(get('--out-dir', 'build/packages'))
const cache = readFileSync(join(build, 'CMakeCache.txt'), 'utf8'),
  type = cache.match(/^CMAKE_BUILD_TYPE:STRING=(.*)$/m)?.[1]
if (type && type.toLowerCase() !== config.toLowerCase())
  throw Error('Configured ' + type + ' SDK cannot be packaged as ' + config)
mkdirSync(out, { recursive: true })
execFileSync(
  process.env.CMAKE || 'cmake',
  ['--build', build, '--target', 'nova_render', '--config', config],
  { stdio: 'inherit' },
)
execFileSync(
  process.env.CPACK || 'cpack',
  ['--config', join(build, 'CPackConfig.cmake'), '-C', config, '-B', out],
  { stdio: 'inherit' },
)
for (const file of readdirSync(out).filter((f) => /^nova-render-sdk-.*\.(tar\.gz|zip)$/.test(f))) {
  const hash = createHash('sha256')
    .update(readFileSync(join(out, file)))
    .digest('hex')
  writeFileSync(join(out, file + '.sha256'), hash + '  ' + file + '\n')
  console.log(join(out, file))
}
