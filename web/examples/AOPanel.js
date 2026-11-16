// Application UI for the AO example; no graphics API calls.
export function attachAOPanel(canvas, scene, cameras, onError) {
  const host = document.createElement('div')
  host.className = 'ao-viewport'
  canvas.before(host)
  const metrics = document.createElement('div')
  metrics.className = 'ao-performance'
  metrics.setAttribute('aria-label', 'Rendering performance')
  metrics.setAttribute('role', 'status')
  metrics.setAttribute('aria-live', 'off')
  metrics.title =
    'FPS measures completed AO updates. Draw/P95 include CPU submission and GPU waits, not pure GPU time. Static views render on demand; idle FPS is —.'
  const viewport = document.createElement('div')
  viewport.className = 'ao-stage'
  viewport.append(canvas)
  host.append(metrics, viewport)
  const style = document.createElement('style')
  style.textContent = `.ao-viewport{width:100%;min-width:0}.ao-performance{box-sizing:border-box;display:flex;flex-wrap:wrap;justify-content:space-between;gap:2px 12px;min-height:38px;padding:5px 8px;background:#151b26;color:#dce5f2;border-radius:6px 6px 0 0;font:11px/1.5 ui-monospace,monospace;font-variant-numeric:tabular-nums;pointer-events:none}.ao-performance span{overflow-wrap:anywhere;min-width:0}.ao-controls{margin-top:12px;border:1px solid #3c4659;border-radius:7px;background:#181e29;color:#e1e6f0;font:12px/1.4 system-ui;color-scheme:dark}.ao-controls-body{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(220px,100%),1fr));gap:14px;padding:12px;max-height:360px;overflow:auto}.ao-controls-body[hidden]{display:none}.ao-controls button,.ao-controls select,.ao-controls input{font:inherit;color:inherit}.ao-controls button,.ao-controls select{background:#25324b;border:1px solid #465776;border-radius:4px;padding:5px}.ao-controls select,.ao-controls input{width:100%;box-sizing:border-box}.ao-controls label{display:flex;flex-direction:column;gap:4px;margin:8px 0}.ao-controls input[type=range]{accent-color:#819eff}.ao-controls legend{font-weight:600;color:#b9ccff}.ao-controls fieldset{min-width:0;border:0;padding:0;margin:0}.ao-toggle{display:block;width:100%;text-align:left}`
  style.textContent += `.ao-stage{position:relative;min-width:0}.ao-controls{max-height:calc(100% - 20px);overflow:auto;position:absolute;top:10px;right:10px;z-index:2;width:min(290px,calc(100% - 20px));margin:0;background:#181e29ed;box-sizing:border-box}.ao-controls:has(.ao-controls-body[hidden]){width:auto;min-width:110px}.ao-controls-body{grid-template-columns:1fr;max-height:min(310px,45vw);overscroll-behavior:contain}`
  host.append(style)
  const panel = document.createElement('section')
  panel.className = 'ao-controls'
  panel.setAttribute('aria-label', 'AO parameters')
  const toggle = document.createElement('button'),
    body = document.createElement('div')
  toggle.className = 'ao-toggle'
  toggle.type = 'button'
  body.className = 'ao-controls-body'
  body.hidden = true
  toggle.textContent = 'Parameters ▸'
  toggle.setAttribute('aria-label', 'Show parameters')
  toggle.setAttribute('aria-expanded', 'false')
  panel.append(toggle, body)
  viewport.append(panel)
  const events = new AbortController(),
    fields = []
  function update(fn) {
    try {
      fn()
      refresh()
    } catch (error) {
      onError(error)
    }
  }
  toggle.addEventListener(
    'click',
    () => {
      body.hidden = !body.hidden
      toggle.textContent = body.hidden ? 'Parameters ▸' : 'Parameters ▾'
      toggle.setAttribute('aria-label', body.hidden ? 'Show parameters' : 'Collapse parameters')
      toggle.setAttribute('aria-expanded', String(!body.hidden))
    },
    { signal: events.signal },
  )
  function group(text) {
    const f = document.createElement('fieldset'),
      l = document.createElement('legend')
    l.textContent = text
    f.append(l)
    body.append(f)
    return f
  }
  function select(parent, title, aria, choices, get, set) {
    const l = document.createElement('label'),
      s = document.createElement('select')
    l.textContent = title
    s.setAttribute('aria-label', aria)
    choices.forEach(([value, text]) => {
      const o = document.createElement('option')
      o.value = value
      o.textContent = text
      s.append(o)
    })
    l.append(s)
    parent.append(l)
    s.addEventListener('change', () => update(() => set(s.value)), { signal: events.signal })
    fields.push(() => {
      s.value = get()
    })
    return s
  }
  const ao = group('Ambient occlusion')
  select(
    ao,
    'Method',
    'AO method',
    [
      ['disabled', 'Off'],
      ['ssao', 'SSAO'],
      ['hbao', 'HBAO'],
      ['msao', 'MSAO'],
      ['rtao', 'RTAO'],
    ],
    () => scene.getAmbientOcclusion().method,
    (v) => scene.setAmbientOcclusion({ ...scene.getAmbientOcclusion(), method: v }),
  )
  select(
    ao,
    'Display',
    'AO output',
    [
      ['composite', 'Composite'],
      ['occlusion', 'AO only'],
      ['depth', 'Depth'],
      ['normals', 'Normals'],
    ],
    () => scene.getAmbientOcclusion().output,
    (v) => scene.setAmbientOcclusion({ ...scene.getAmbientOcclusion(), output: v }),
  )
  const sampling = group('Sampling')
  for (const [key, title, aria, min, max, step] of [
    ['radius', 'Radius', 'AO radius', 0.001, 1, 0.001],
    ['bias', 'Bias', 'AO bias', 0, 0.05, 0.0005],
    ['strength', 'Strength', 'AO strength', 0, 4, 0.05],
    ['samples', 'Samples', 'AO samples', 4, 64, 4],
    ['resolutionScale', 'Render scale', 'Render scale', 0.25, 1, 0.05],
  ]) {
    const l = document.createElement('label'),
      caption = document.createElement('span'),
      e = document.createElement('input')
    e.type = 'range'
    Object.assign(e, { min, max, step })
    e.setAttribute('aria-label', aria)
    l.append(caption, e)
    sampling.append(l)
    e.addEventListener(
      'input',
      () =>
        update(() =>
          scene.setAmbientOcclusion({ ...scene.getAmbientOcclusion(), [key]: Number(e.value) }),
        ),
      { signal: events.signal },
    )
    fields.push(() => {
      const value = scene.getAmbientOcclusion()[key]
      e.value = value
      caption.textContent = title + ' · ' + value
    })
  }
  const denoiseLabel = document.createElement('label'),
    denoise = document.createElement('input')
  denoise.type = 'checkbox'
  denoise.setAttribute('aria-label', 'AO filtering')
  denoiseLabel.textContent = 'Edge-aware filter'
  denoiseLabel.append(denoise)
  sampling.append(denoiseLabel)
  denoise.addEventListener(
    'change',
    () =>
      update(() =>
        scene.setAmbientOcclusion({ ...scene.getAmbientOcclusion(), denoise: denoise.checked }),
      ),
    { signal: events.signal },
  )
  fields.push(() => {
    denoise.checked = scene.getAmbientOcclusion().denoise
  })
  const adaptiveLabel = document.createElement('label'),
    adaptive = document.createElement('input')
  adaptive.type = 'checkbox'
  adaptive.setAttribute('aria-label', 'Adaptive interaction quality')
  adaptiveLabel.textContent = 'Adaptive interaction quality'
  adaptiveLabel.append(adaptive)
  sampling.append(adaptiveLabel)
  adaptive.addEventListener(
    'change',
    () => update(() => scene.setAdaptiveQuality(adaptive.checked)),
    { signal: events.signal },
  )
  fields.push(() => {
    adaptive.checked = scene.getAdaptiveQuality()
  })
  const camera = group('Camera')
  let current = 'courtyard'
  select(
    camera,
    'View',
    'AO camera',
    [
      ['courtyard', 'Column arcade'],
      ['arcade', 'Doorway'],
      ['overview', 'Overview'],
    ],
    () => current,
    (v) => {
      scene.setCamera(cameras[v])
      current = v
    },
  )
  const status = document.createElement('div')
  status.setAttribute('aria-label', 'AO accumulation')
  camera.append(status)
  fields.push(() => {
    status.textContent = `Frames: ${scene.getAccumulatedSamples()}${scene.getAmbientOcclusion().method === 'rtao' ? ' / 32' : ''}`
  })
  function performanceInfo() {
    const p = scene.getPerformanceStats(),
      fps = p.fps === null ? '—' : p.fps.toFixed(1),
      left = document.createElement('span'),
      right = document.createElement('span')
    left.textContent = `AO ${fps} FPS · Draw ${p.frameCount ? p.lastRenderMs.toFixed(1) : '—'} ms · P95 ${p.frameCount > 1 ? p.p95Ms.toFixed(1) : '—'} ms`
    const state = {
      idle: 'Idle · on demand',
      paused: 'Hidden · paused',
      interactive: p.interactive ? 'Interaction preview' : 'Interactive rendering',
      accumulating: 'Accumulating',
      rendering: 'Rendering',
    }[p.state]
    right.textContent = `${state} · ${p.width}×${p.height} · ${(p.scale * 100).toFixed(1)}% · ${p.method.toUpperCase()} · ${p.samples} samples`
    metrics.title = `FPS measures completed AO updates; Draw includes CPU submission and GPU waits. P95 covers the last 60 non-initial draws. First frame: ${p.firstRenderMs.toFixed(1)} ms (including resource preparation). Idle views render on demand. Interaction previews temporarily reduce resolution and samples, then restore the chosen settings.`
    metrics.replaceChildren(left, right)
  }
  const metricTimer = setInterval(performanceInfo, 250)
  function refresh() {
    fields.forEach((f) => f())
    performanceInfo()
  }
  refresh()
  return {
    refresh,
    dispose() {
      clearInterval(metricTimer)
      events.abort()
      host.before(canvas)
      host.remove()
    },
  }
}
