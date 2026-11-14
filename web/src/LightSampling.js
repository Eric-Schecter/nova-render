// CPU distributions only; both native and web evaluate the same PDFs in pathtrace.hlsl.
const luma = (r, g, b) => 0.2126 * r + 0.7152 * g + 0.0722 * b
const half = (h) =>
  (h & 32768 ? -1 : 1) *
  ((h >> 10) & 31 ? 1 + (h & 1023) / 1024 : (h & 1023) / 1024) *
  2 ** ((h >> 10) & 31 ? ((h >> 10) & 31) - 15 : -14)
const corner = (u, v) => Math.atan2(u * v, Math.sqrt(u * u + v * v + 1))
function solidAngle(x, y, size) {
  const u = (2 * x) / size - 1,
    v = (2 * y) / size - 1,
    d = 2 / size
  return corner(u + d, v + d) - corner(u, v + d) - corner(u + d, v) + corner(u, v)
}
function cdf(weights) {
  const total = weights.reduce((a, b) => a + b, 0),
    result = new Float32Array(weights.length)
  let sum = 0
  weights.forEach((w, i) => (result[i] = (sum += w) / total))
  if (result.length) result[result.length - 1] = 1
  return result
}
export function buildLightSampling(data, images, vertices, originalIds) {
  const image = data.images[4],
    pixels = images[4],
    factor = data.emissive
  let mean = 0
  for (let i = 0; i < pixels.length; i += 4)
    mean += luma(
      (pixels[i] / 255) ** 2.2 * factor[0],
      (pixels[i + 1] / 255) ** 2.2 * factor[1],
      (pixels[i + 2] / 255) ** 2.2 * factor[2],
    )
  mean /= Math.max(1, pixels.length / 4)
  function emission(u, v) {
    const x = (u - Math.floor(u)) * image.width - 0.5,
      y = (v - Math.floor(v)) * image.height - 0.5,
      bx = Math.floor(x),
      by = Math.floor(y),
      rgb = [0, 0, 0]
    for (let j = 0; j < 2; j++)
      for (let i = 0; i < 2; i++) {
        const ix = (((bx + i) % image.width) + image.width) % image.width,
          iy = (((by + j) % image.height) + image.height) % image.height,
          w = (i ? x - bx : 1 - x + bx) * (j ? y - by : 1 - y + by)
        for (let k = 0; k < 3; k++) rgb[k] += (w * pixels[(iy * image.width + ix) * 4 + k]) / 255
      }
    return luma(rgb[0] ** 2.2 * factor[0], rgb[1] ** 2.2 * factor[1], rgb[2] ** 2.2 * factor[2])
  }
  const records = [],
    weights = []
  if (mean > 0)
    for (let id = 0; id < vertices.length / 36; id++) {
      const a = id * 36,
        b = a + 12,
        c = a + 24,
        e = [0, 1, 2].map((k) => vertices[b + k] - vertices[a + k]),
        f = [0, 1, 2].map((k) => vertices[c + k] - vertices[a + k]),
        cross = [0, 1, 2].map(
          (k) => e[(k + 1) % 3] * f[(k + 2) % 3] - e[(k + 2) % 3] * f[(k + 1) % 3],
        ),
        area = 0.5 * Math.hypot(...cross)
      if (area <= 1e-16) continue
      const value =
        (emission(vertices[a + 8], vertices[a + 9]) +
          emission(vertices[b + 8], vertices[b + 9]) +
          emission(vertices[c + 8], vertices[c + 9]) +
          emission(
            (vertices[a + 8] + vertices[b + 8] + vertices[c + 8]) / 3,
            (vertices[a + 9] + vertices[b + 9] + vertices[c + 9]) / 3,
          )) /
        4
      records.push({
        id,
        area,
        weight: area * Math.max(value, mean * 1e-4),
        original: originalIds[id],
      })
    }
  records.sort((a, b) => a.original - b.original)
  records.forEach((r) => weights.push(r.weight))
  const distribution = cdf(weights),
    count = vertices.length / 36
  // Header + emitter records + per-primitive PMFs + maximum 64² cubemap CDF.
  const words = new Uint32Array(8 + records.length * 4 + count + 6 * 64 * 64),
    floats = new Float32Array(words.buffer)
  words.set([records.length, 8, 8 + records.length * 4, 8 + records.length * 4 + count])
  floats[5] = 2 * Math.PI * weights.reduce((a, b) => a + b, 0)
  records.forEach((r, i) => {
    const pmf = distribution[i] - (i ? distribution[i - 1] : 0),
      at = 8 + i * 4
    floats[at] = distribution[i]
    floats[at + 1] = pmf
    words[at + 2] = r.id
    floats[at + 3] = r.area
    floats[words[2] + r.id] = pmf
  })
  return {
    words,
    updateEnvironment(sky, hdr) {
      const grid = hdr ? Math.min(64, sky.size) : 64,
        size = hdr ? sky.size : grid,
        weights = new Float64Array(6 * grid * grid),
        solid = new Float64Array(weights.length)
      let power = 0
      const omega = Array.from({ length: size * size }, (_, i) =>
        solidAngle(i % size, Math.floor(i / size), size),
      )
      for (let face = 0; face < 6; face++)
        for (let y = 0; y < size; y++)
          for (let x = 0; x < size; x++) {
            const angle = omega[y * size + x]
            let value
            if (hdr) {
              const i = ((face * size + y) * size + x) * 4
              value = luma(half(sky.pixels[i]), half(sky.pixels[i + 1]), half(sky.pixels[i + 2]))
            } else {
              const u = (2 * (x + 0.5)) / size - 1,
                v = (2 * (y + 0.5)) / size - 1,
                dy = (face === 2 ? 1 : face === 3 ? -1 : -v) / Math.sqrt(u * u + v * v + 1),
                t = dy * 0.5 + 0.5
              value = luma(
                0.025 + (0.45 - 0.025) * t,
                0.03 + (0.52 - 0.03) * t,
                0.04 + (0.65 - 0.04) * t,
              )
            }
            const cell =
              (face * grid + Math.floor((y * grid) / size)) * grid + Math.floor((x * grid) / size)
            weights[cell] += Math.max(0, value) * angle
            solid[cell] += angle
            power += Math.max(0, value) * angle
          }
      for (let i = 0; i < weights.length; i++)
        weights[i] =
          power > 0 ? (0.95 * weights[i]) / power + (0.05 * solid[i]) / (4 * Math.PI) : solid[i]
      words[4] = power > 0 ? grid : 0
      floats[6] = power
      floats.set(cdf(weights), words[3])
    },
  }
}
