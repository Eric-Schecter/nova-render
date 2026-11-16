import { execFileSync } from 'node:child_process'
import { dirname, join } from 'node:path'
export function runNpm(args, options = {}) {
  let cli = process.env.npm_execpath
  if (!cli && process.platform === 'win32') {
    const command = execFileSync('where.exe', ['npm.cmd'], { encoding: 'utf8' })
      .trim()
      .split(/\r?\n/)[0]
    cli = join(dirname(command), 'node_modules/npm/bin/npm-cli.js')
  }
  return cli
    ? execFileSync(process.execPath, [cli, ...args], options)
    : execFileSync('npm', args, options)
}
