import { inflateSync } from 'node:zlib'
export function decode(png) {
  const width = png.readUInt32BE(16),
    height = png.readUInt32BE(20),
    channels = png[25] === 6 ? 4 : 3
  if (png[24] !== 8 || ![2, 6].includes(png[25]) || png[28] !== 0)
    throw Error('Unsupported screenshot PNG')
  const chunks = []
  for (let offset = 8; offset < png.length; ) {
    const size = png.readUInt32BE(offset)
    if (png.toString('ascii', offset + 4, offset + 8) === 'IDAT')
      chunks.push(png.subarray(offset + 8, offset + 8 + size))
    offset += size + 12
  }
  const raw = inflateSync(Buffer.concat(chunks)),
    stride = width * channels,
    pixels = Buffer.alloc(height * stride)
  for (let y = 0; y < height; y++) {
    const filter = raw[y * (stride + 1)]
    for (let x = 0; x < stride; x++) {
      const k = y * stride + x,
        a = x >= channels ? pixels[k - channels] : 0,
        b = y ? pixels[k - stride] : 0,
        c = y && x >= channels ? pixels[k - stride - channels] : 0
      const p = a + b - c,
        pa = Math.abs(p - a),
        pb = Math.abs(p - b),
        pc = Math.abs(p - c)
      const predictor =
        filter === 0
          ? 0
          : filter === 1
            ? a
            : filter === 2
              ? b
              : filter === 3
                ? Math.floor((a + b) / 2)
                : filter === 4
                  ? pa <= pb && pa <= pc
                    ? a
                    : pb <= pc
                      ? b
                      : c
                  : NaN
      if (!Number.isFinite(predictor)) throw Error('Invalid PNG filter')
      pixels[k] = (raw[y * (stride + 1) + x + 1] + predictor) & 255
    }
  }
  return { width, height, channels, pixels }
}
