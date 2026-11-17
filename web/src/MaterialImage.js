// Preserve straight RGB even when alpha is zero. Canvas decoding premultiplies
// those texels and loses their RGB, changing OPAQUE and runtime alpha-mode edits.
async function decodePNG(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
  let width, height, depth, type, interlace, palette, transparency
  const compressed = []
  for (let offset = 8; offset < bytes.length; ) {
    if (offset + 12 > bytes.length) throw Error('Truncated PNG')
    const length = view.getUint32(offset),
      end = offset + 12 + length
    if (end > bytes.length) throw Error('Truncated PNG chunk')
    const name = String.fromCharCode(...bytes.subarray(offset + 4, offset + 8)),
      data = bytes.subarray(offset + 8, end - 4)
    if (name === 'IHDR') {
      if (length !== 13 || width !== undefined) throw Error('Invalid PNG header')
      width = view.getUint32(offset + 8)
      height = view.getUint32(offset + 12)
      depth = data[8]
      type = data[9]
      interlace = data[12]
      if (
        !width ||
        !height ||
        width > 8192 ||
        height > 8192 ||
        data[10] ||
        data[11] ||
        interlace > 1
      )
        throw Error('Unsupported PNG header')
    } else if (name === 'PLTE') palette = data
    else if (name === 'tRNS') transparency = data
    else if (name === 'IDAT') compressed.push(data)
    else if (name === 'IEND') break
    offset = end
  }
  const channels = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 }[type]
  if (
    !width ||
    !channels ||
    !{ 0: [1, 2, 4, 8, 16], 2: [8, 16], 3: [1, 2, 4, 8], 4: [8, 16], 6: [8, 16] }[type].includes(
      depth,
    ) ||
    !compressed.length
  )
    throw Error('Unsupported PNG format')
  if (type === 3 && (!palette || palette.length % 3)) throw Error('Missing PNG palette')
  const passes = interlace
    ? [
        [0, 0, 8, 8],
        [4, 0, 8, 8],
        [0, 4, 4, 8],
        [2, 0, 4, 4],
        [0, 2, 2, 4],
        [1, 0, 2, 2],
        [0, 1, 1, 2],
      ]
    : [[0, 0, 1, 1]]
  const sizes = passes.map(([x, y, dx, dy]) => [
    Math.max(0, Math.ceil((width - x) / dx)),
    Math.max(0, Math.ceil((height - y) / dy)),
  ])
  const expected = sizes.reduce(
    (n, [w, h]) => n + (w && h ? (Math.ceil((w * channels * depth) / 8) + 1) * h : 0),
    0,
  )
  const stream = new Blob(compressed)
    .stream()
    .pipeThrough(new DecompressionStream('deflate'))
    .getReader()
  const inflated = new Uint8Array(expected)
  let total = 0
  try {
    for (;;) {
      const { done, value } = await stream.read()
      if (done) break
      if (total + value.length > expected) throw Error('PNG decompressed size mismatch')
      inflated.set(value, total)
      total += value.length
    }
  } finally {
    await stream.cancel()
  }
  if (total !== expected) throw Error('PNG decompressed size mismatch')
  const pixels = new Uint8Array(width * height * 4),
    bpp = Math.max(1, Math.ceil((channels * depth) / 8)),
    max = (1 << Math.min(depth, 16)) - 1
  const tr = transparency
    ? new DataView(transparency.buffer, transparency.byteOffset, transparency.byteLength)
    : null
  let offset = 0
  const paeth = (a, b, c) => {
    const p = a + b - c,
      pa = Math.abs(p - a),
      pb = Math.abs(p - b),
      pc = Math.abs(p - c)
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c
  }
  passes.forEach(([x0, y0, dx, dy], pass) => {
    const [w, h] = sizes[pass]
    if (!w || !h) return
    const stride = Math.ceil((w * channels * depth) / 8)
    let previous = new Uint8Array(stride)
    for (let y = 0; y < h; y++) {
      const filter = inflated[offset++],
        row = inflated.slice(offset, offset + stride)
      offset += stride
      if (filter > 4) throw Error('Invalid PNG filter')
      for (let i = 0; i < stride; i++) {
        const a = i >= bpp ? row[i - bpp] : 0,
          b = previous[i],
          c = i >= bpp ? previous[i - bpp] : 0
        row[i] =
          (row[i] +
            (filter === 1
              ? a
              : filter === 2
                ? b
                : filter === 3
                  ? Math.floor((a + b) / 2)
                  : filter === 4
                    ? paeth(a, b, c)
                    : 0)) &
          255
      }
      const sample = (i) =>
        depth === 16
          ? (row[i * 2] << 8) | row[i * 2 + 1]
          : depth === 8
            ? row[i]
            : (row[Math.floor((i * depth) / 8)] >> (8 - depth - ((i * depth) % 8))) & max
      const byte = (v) => (depth === 16 ? v >>> 8 : Math.round((v * 255) / max))
      for (let x = 0; x < w; x++) {
        const i = x * channels,
          p = ((y0 + y * dy) * width + x0 + x * dx) * 4,
          a = sample(i)
        if (type === 3) {
          if (a * 3 + 2 >= palette.length) throw Error('PNG palette index out of bounds')
          pixels.set(palette.subarray(a * 3, a * 3 + 3), p)
          pixels[p + 3] = transparency?.[a] ?? 255
        } else if (type === 0 || type === 4) {
          pixels[p] = pixels[p + 1] = pixels[p + 2] = byte(a)
          pixels[p + 3] =
            type === 4
              ? byte(sample(i + 1))
              : tr?.byteLength >= 2 && a === tr.getUint16(0)
                ? 0
                : 255
        } else {
          const b = sample(i + 1),
            c = sample(i + 2)
          pixels[p] = byte(a)
          pixels[p + 1] = byte(b)
          pixels[p + 2] = byte(c)
          pixels[p + 3] =
            type === 6
              ? byte(sample(i + 3))
              : tr?.byteLength >= 6 &&
                  a === tr.getUint16(0) &&
                  b === tr.getUint16(2) &&
                  c === tr.getUint16(4)
                ? 0
                : 255
        }
      }
      previous = row
    }
  })
  return { width, height, pixels }
}
export async function decodeMaterialImage(blob) {
  const bytes = new Uint8Array(await blob.arrayBuffer())
  if (bytes.length >= 8 && [137, 80, 78, 71, 13, 10, 26, 10].every((v, i) => bytes[i] === v))
    return decodePNG(bytes)
  const bitmap = await createImageBitmap(blob, {
    colorSpaceConversion: 'none',
    premultiplyAlpha: 'none',
  })
  try {
    if (!bitmap.width || !bitmap.height || bitmap.width > 8192 || bitmap.height > 8192)
      throw Error('Material image dimensions must be 1..8192')
    const canvas = new OffscreenCanvas(bitmap.width, bitmap.height),
      ctx = canvas.getContext('2d')
    ctx.drawImage(bitmap, 0, 0)
    return {
      width: bitmap.width,
      height: bitmap.height,
      pixels: new Uint8Array(ctx.getImageData(0, 0, bitmap.width, bitmap.height).data),
    }
  } finally {
    bitmap.close()
  }
}
