#!/usr/bin/env node
import { cpSync, mkdirSync, readdirSync } from 'node:fs'
import { resolve, join } from 'node:path'
import { fileURLToPath } from 'node:url'
const args = process.argv.slice(2),
  index = args.indexOf('--out')
if (index < 0 || !args[index + 1]) throw Error('Usage: nova-web-assets --out public/nova')
const out = resolve(args[index + 1]),
  source = fileURLToPath(new URL('../assets/', import.meta.url))
mkdirSync(out, { recursive: true })
for (const file of readdirSync(source)) cpSync(join(source, file), join(out, file))
console.log('Nova precompiled shader assets copied to ' + out)
