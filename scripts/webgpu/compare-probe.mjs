import { readFileSync } from 'node:fs'
const files = process.argv.slice(2)
if (files.length < 2) throw Error('Provide at least two probe-<backend>.json fixtures')
const reference = JSON.parse(readFileSync(files[0], 'utf8'))
if (reference.length !== 8) throw Error('Expected eight probe variants')
for (const path of files.slice(1)) {
  const images = JSON.parse(readFileSync(path, 'utf8'))
  if (images.length !== reference.length) throw Error('Probe variant count differs: ' + path)
  const errors = images.map((image, mode) => {
    const expected = reference[mode]
    if (image.length !== expected.length || !image.every(Number.isFinite))
      throw Error('Invalid fixture ' + path)
    const mae =
      image.reduce((sum, value, i) => sum + Math.abs(value - expected[i]), 0) / image.length
    if (mae > 2)
      throw Error('Probe backend image mismatch ' + path + ' variant ' + mode + ': ' + mae)
    return mae
  })
  console.log('PASS probe backend parity', path, errors)
}
