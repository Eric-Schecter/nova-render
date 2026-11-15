const srgbDecode = Float64Array.from({ length: 256 }, (_, i) => {
  const c = i / 255
  return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4
})
// Matches native material mip generation: linear-light color, linear data, full NPOT coverage.
export function materialMipmaps(image, slot) {
  let { width, height, pixels } = image
  if (!pixels) {
    const canvas = new OffscreenCanvas(width, height),
      ctx = canvas.getContext('2d', { willReadFrequently: true })
    ctx.drawImage(image.bitmap, 0, 0)
    pixels = ctx.getImageData(0, 0, width, height).data
  }
  const levels = [{ width, height, pixels }],
    color = slot === 0 || slot === 4
  while (width > 1 || height > 1) {
    const old = levels.at(-1),
      w = Math.max(1, Math.floor(width / 2)),
      h = Math.max(1, Math.floor(height / 2)),
      data = new Uint8Array(w * h * 4)
    for (let y = 0; y < h; y++)
      for (let x = 0; x < w; x++) {
        const x0 = (x * width) / w,
          x1 = ((x + 1) * width) / w,
          y0 = (y * height) / h,
          y1 = ((y + 1) * height) / h,
          value = [0, 0, 0, 0]
        let sum = 0
        for (let sy = Math.floor(y0); sy < Math.min(height, Math.ceil(y1)); sy++)
          for (let sx = Math.floor(x0); sx < Math.min(width, Math.ceil(x1)); sx++) {
            const weight =
              (Math.min(x1, sx + 1) - Math.max(x0, sx)) * (Math.min(y1, sy + 1) - Math.max(y0, sy))
            sum += weight
            for (let c = 0; c < 4; c++) {
              let v = old.pixels[(sy * width + sx) * 4 + c] / 255
              if (color && c < 3) v = srgbDecode[old.pixels[(sy * width + sx) * 4 + c]]
              value[c] += v * weight
            }
          }
        for (let c = 0; c < 4; c++) {
          let v = value[c] / sum
          if (color && c < 3) v = v <= 0.0031308 ? v * 12.92 : 1.055 * v ** (1 / 2.4) - 0.055
          data[(y * w + x) * 4 + c] = Math.round(Math.max(0, Math.min(1, v)) * 255)
        }
      }
    width = w
    height = h
    levels.push({ width, height, pixels: data })
  }
  return levels
}
