import { runNpm } from './process.mjs'
import { execFileSync, spawn } from 'node:child_process'
import { mkdtempSync, cpSync, mkdirSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import { chromium } from 'playwright'
const archive = resolve(process.argv[2]),
  work = mkdtempSync(join(tmpdir(), 'nova-npm-consumer-'))
cpSync(new URL('../../examples/npm-consumer/', import.meta.url), work, { recursive: true })
runNpm(
  [
    'install',
    archive,
    '--offline',
    '--cache',
    join(work, 'npm-cache'),
    '--ignore-scripts',
    '--no-audit',
    '--no-fund',
  ],
  { cwd: work, stdio: 'inherit' },
)
execFileSync(
  process.execPath,
  [
    '--input-type=module',
    '-e',
    "import {createRenderer,createReflectionProbeView} from '@nova/render-web';if(typeof createRenderer!=='function'||typeof createReflectionProbeView!=='function')throw Error('Missing package exports')",
  ],
  { cwd: work, stdio: 'inherit' },
)
execFileSync(
  process.execPath,
  [join(work, 'node_modules/@nova/render-web/bin/copy-assets.mjs'), '--out', 'public/nova'],
  { cwd: work, stdio: 'inherit' },
)
const server = spawn(process.execPath, ['server.mjs'], {
  cwd: work,
  stdio: ['ignore', 'pipe', 'inherit'],
})
let browser
try {
  const url = await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(Error('Consumer server timeout')), 15000)
    server.on('exit', (code) => {
      clearTimeout(timer)
      reject(Error('Consumer server exited ' + code))
    })
    server.stdout.on('data', (data) => {
      const match = data.toString().match(/READY (http:\/\/\S+)/)
      if (match) {
        clearTimeout(timer)
        resolve(match[1])
      }
    })
  })
  browser = await chromium.launch({
    channel: 'chrome',
    headless: true,
    args: ['--enable-unsafe-webgpu'],
  })
  const page = await browser.newPage(),
    errors = [],
    requests = []
  page.on('pageerror', (error) => errors.push(error.message))
  page.on('request', (req) => requests.push(req.url()))
  await page.goto(url)
  const result = await page.evaluate(() => window.validation)
  if (errors.length) throw Error(errors.join('\n'))
  if (
    requests.some(
      (url) =>
        url.includes('/__nova/compile') || url.includes('/@fs') || url.includes('/generated/'),
    )
  )
    throw Error('Consumer escaped installed package/assets')
  mkdirSync('build/validation/package', { recursive: true })
  await page.screenshot({ path: 'build/validation/package/npm-consumer.png' })
  await page.evaluate(() => window.disposeNova())
  writeFileSync(
    'build/validation/package/npm-consumer.json',
    JSON.stringify({ work, result, requests }, null, 2),
  )
  console.log('PASS standalone npm install and WebGPU APIs:', work, result)
} finally {
  await browser?.close()
  server.kill()
}
