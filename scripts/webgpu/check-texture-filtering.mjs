import assert from 'node:assert/strict'
import { materialMipmaps } from '../../web/src/TextureFiltering.js'
const image = { width: 2, height: 1, pixels: new Uint8Array([0, 0, 0, 0, 255, 255, 255, 255]) }
assert.equal(materialMipmaps(image, 0)[1].pixels[0], 188)
assert.equal(materialMipmaps(image, 4)[1].pixels[0], 188)
assert.equal(materialMipmaps(image, 2)[1].pixels[0], 128)
assert.equal(materialMipmaps(image, 0)[1].pixels[3], 128)
assert.equal(
  materialMipmaps(
    {
      width: 3,
      height: 1,
      pixels: new Uint8Array([0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 255]),
    },
    2,
  )[1].pixels[0],
  85,
)
console.log('PASS color-space, data/alpha and NPOT texture mips')
