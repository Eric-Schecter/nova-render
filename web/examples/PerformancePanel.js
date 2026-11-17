import { createFrameMeter } from './FrameMeter.js'

// Example UI: independent layout strip, no GPU queries, no quality policy changes.
export function attachPerformanceBar(host, getStats) {
  let style = document.getElementById('nova-performance-style')
  if (!style) {
    style = document.createElement('style')
    style.id = 'nova-performance-style'
    style.textContent = `.nova-performance{box-sizing:border-box;display:flex;flex-wrap:wrap;justify-content:space-between;gap:2px 12px;min-height:40px;padding:5px 8px;background:#151b26;color:#dce5f2;border-radius:6px 6px 0 0;font:11px/1.5 ui-monospace,monospace;font-variant-numeric:tabular-nums;pointer-events:none}.nova-performance span{min-width:0;overflow-wrap:anywhere}.nova-performance-host{width:100%;min-width:0}`
    document.head.append(style)
  }
  const bar = document.createElement('div'),
    left = document.createElement('span'),
    right = document.createElement('span')
  bar.className = 'nova-performance'
  bar.setAttribute('aria-label', 'Rendering performance')
  bar.title =
    'FPS measures completed image updates. Draw includes CPU preparation, queuing and GPU waits. P95 covers the last 60 non-initial draws. Idle and hidden views show — FPS.'
  bar.append(left, right)
  host.prepend(bar)
  function refresh() {
    const p = getStats()
    left.textContent = `${p.method || 'Render'} ${p.fps == null ? '—' : p.fps.toFixed(1)} FPS · Draw ${p.frameCount ? p.lastRenderMs.toFixed(1) : '—'} ms · P95 ${p.frameCount > 1 ? p.p95Ms.toFixed(1) : '—'} ms`
    const state =
      {
        idle: 'Idle · on demand',
        paused: 'Hidden · paused',
        rendering: 'Rendering',
        accumulating: 'Accumulating',
      }[p.state] || p.state
    right.textContent = `${state} · ${p.width}×${p.height} · ${(p.scale * 100).toFixed(1)}% · ${p.samples} samples${p.accumulatedFrames ? ` · ${p.accumulatedFrames} accumulated` : ''}`
  }
  const timer = setInterval(refresh, 250)
  refresh()
  return {
    refresh,
    dispose() {
      clearInterval(timer)
      bar.remove()
    },
  }
}

export function instrumentRenderer(renderer, canvas) {
  const host = document.createElement('div')
  host.className = 'nova-performance-host'
  // Preserve explicit consumer canvas widths, including integration tests.
  if (canvas.style.width && canvas.style.width !== '100%') {
    host.style.width = canvas.style.width
    host.style.maxWidth = '100%'
  }
  canvas.before(host)
  host.append(canvas)
  const meter = createFrameMeter(),
    draw = renderer.drawFrame.bind(renderer),
    dispose = renderer.dispose.bind(renderer)
  let pending = 0,
    lastFinished = -Infinity,
    disposed = false,
    continuous = true
  const setContinuous = renderer.setContinuousRendering.bind(renderer)
  renderer.setContinuousRendering = (value = true) => {
    setContinuous(value)
    continuous = value
  }
  const visible = () => {
    const r = canvas.getBoundingClientRect()
    return (
      !document.hidden &&
      !canvas.closest('[aria-hidden="true"],[inert]') &&
      r.width > 0 &&
      r.height > 0 &&
      r.bottom > 0 &&
      r.top < innerHeight
    )
  }
  renderer.getPerformanceStats = () => {
    const animation = renderer.getAnimationState(),
      ray = renderer.getRenderMode() === 'ray-tracing'
    const active =
      pending > 0 ||
      (continuous && (ray || (renderer.getAnimations().length > 0 && animation?.playing))) ||
      (meter.snapshot('rendering').frameCount > 1 && performance.now() - lastFinished < 200)
    return {
      ...meter.snapshot(
        !visible() ? 'paused' : !active ? 'idle' : ray ? 'accumulating' : 'rendering',
      ),
      accumulatedFrames:
        renderer.getRenderMode() === 'hybrid'
          ? renderer.getHybridHistoryFrames()
          : renderer.getRenderMode() !== 'ray-tracing'
            ? renderer.getAntiAliasingHistoryFrames()
            : renderer.getAccumulatedSamples(),
    }
  }
  const panel = attachPerformanceBar(host, () => renderer.getPerformanceStats())
  renderer.drawFrame = async () => {
    const start = performance.now()
    pending++
    try {
      const result = await draw()
      if (result === 'rendered' && !disposed) {
        const ray = renderer.getRenderMode() === 'ray-tracing',
          settings = renderer.getRayTracing(),
          aa = renderer.getAntiAliasing(),
          scale = ray ? settings.resolutionScale : aa.method === 'ssaa' ? 2 : 1
        meter.record(start, {
          width: Math.max(1, Math.round(canvas.width * scale)),
          height: Math.max(1, Math.round(canvas.height * scale)),
          scale,
          samples: ray ? settings.samplesPerFrame : ['msaa', 'ssaa'].includes(aa.method) ? 4 : 1,
          method:
            renderer.getRenderMode() === 'hybrid'
              ? 'Hybrid'
              : ray
                ? {
                    path: 'PT',
                    bidirectional: 'BDPT',
                    'photon-mapping': 'Photon mapping',
                    mlt: 'MLT',
                  }[settings.integrator]
                : {
                    off: 'Raster',
                    fxaa: 'FXAA',
                    smaa: 'SMAA 1x',
                    msaa: 'MSAA 4x',
                    ssaa: 'SSAA 2×2',
                    taa: 'TAA',
                  }[aa.method],
        })
        lastFinished = performance.now()
      }
      return result
    } finally {
      pending--
      if (!disposed) panel.refresh()
    }
  }
  renderer.dispose = () => {
    if (disposed) return
    disposed = true
    panel.dispose()
    dispose()
    host.before(canvas)
    host.remove()
  }
  return renderer
}
