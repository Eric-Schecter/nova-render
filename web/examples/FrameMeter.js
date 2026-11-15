// Example metrics: actual completed image updates, not monitor refresh or pure GPU time.
export function createFrameMeter(clock = () => performance.now()) {
  const durations = [],
    completed = []
  let count = 0,
    firstRenderMs = 0,
    latest = { lastRenderMs: 0, width: 0, height: 0, scale: 0, samples: 0, method: '' }
  return {
    record(start, details) {
      const now = clock(),
        ms = Math.max(0, now - start)
      count++
      latest = { ...details, lastRenderMs: ms }
      if (count === 1) firstRenderMs = ms
      else durations.push(ms)
      completed.push(now)
      if (durations.length > 60) durations.shift()
      while (completed.length > 2 && completed[1] < now - 1000) completed.shift()
      return ms
    },
    snapshot(state) {
      const now = clock()
      while (completed.length > 2 && completed[1] < now - 1000) completed.shift()
      const active = !['idle', 'paused'].includes(state)
      if (!active) completed.length = 0
      const sorted = [...durations].sort((a, b) => a - b)
      const span = completed.length > 1 ? completed.at(-1) - completed[0] : 0
      return {
        ...latest,
        frameCount: count,
        firstRenderMs,
        state,
        fps: active && span > 0 ? (1000 * (completed.length - 1)) / span : null,
        p95Ms: sorted.length ? sorted[Math.ceil(sorted.length * 0.95) - 1] : 0,
      }
    },
  }
}
