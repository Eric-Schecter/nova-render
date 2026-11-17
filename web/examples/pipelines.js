import * as Nova from '../src/Nova.js'
import { createFrameMeter } from './FrameMeter.js'
import { attachPerformanceBar } from './PerformancePanel.js'
// Example owns presentation and controls; Nova owns every rendering pass.
export async function showPipelineComparison(
  canvas,
  shaderDirectory = '/generated/',
  { onError } = {},
) {
  const view = await Nova.createPipelineComparisonView({ canvas, shaderDirectory })
  const host = document.createElement('div'),
    viewport = document.createElement('div')
  host.className = 'pipeline-laboratory'
  viewport.className = 'pipeline-viewport'
  canvas.before(host)
  host.append(viewport)
  viewport.append(canvas)
  const style = document.createElement('style')
  style.textContent = `.pipeline-laboratory{width:100%;color:#dce5f2;font:12px/1.5 system-ui}.pipeline-viewport{position:relative}.pipeline-viewport canvas{display:block;width:100%;height:auto;aspect-ratio:1.65;background:#040507;touch-action:none}.pipeline-controls{position:absolute;top:10px;right:10px;max-width:260px;background:#181e29ef;border:1px solid #3c4659;border-radius:7px}.pipeline-controls summary{padding:7px 10px;cursor:pointer}.pipeline-controls section{display:grid;gap:8px;padding:10px;max-height:260px;overflow:auto}.pipeline-controls label{display:grid;gap:3px}.pipeline-controls select,.pipeline-controls input{font:inherit;color:inherit;background:#25324b;max-width:100%;padding:4px}.pipeline-controls input[type=range]{width:220px;accent-color:#819eff}`
  host.append(style)
  const drawer = document.createElement('details'),
    summary = document.createElement('summary'),
    panel = document.createElement('section')
  drawer.className = 'pipeline-controls'
  summary.textContent = 'Parameters'
  drawer.append(summary, panel)
  viewport.append(drawer)
  const meter = createFrameMeter()
  let disposed = false,
    busy = false,
    dirty = true,
    animation = 0,
    continuous = false
  const events = new AbortController()
  const bar = attachPerformanceBar(host, () =>
    meter.snapshot(busy || continuous ? 'rendering' : 'idle'),
  )
  const visible = () =>
    !document.hidden &&
    !host.closest('[inert],[aria-hidden="true"]') &&
    canvas.getBoundingClientRect().bottom > 0 &&
    canvas.getBoundingClientRect().top < innerHeight
  const report = (e) => {
    continuous = false
    onError?.(e)
  }
  async function drawFrame() {
    if (disposed || busy) {
      dirty = true
      return
    }
    busy = true
    dirty = false
    try {
      const w = Math.max(
          160,
          Math.min(1024, Math.round(canvas.getBoundingClientRect().width || 800)),
        ),
        h = Math.round(w / 1.65)
      const start = performance.now()
      await view.drawFrame(w, h)
      meter.record(start, {
        width: w,
        height: h,
        scale: 1,
        samples: 1,
        method: view.getSettings().pipeline,
      })
      bar.refresh()
    } finally {
      busy = false
    }
  }
  function select(label, key, values, labels) {
    const field = document.createElement('label'),
      input = document.createElement('select')
    field.textContent = label
    input.setAttribute('aria-label', label)
    values.forEach((v, i) => {
      const option = document.createElement('option')
      option.value = v
      option.textContent = labels[i]
      input.append(option)
    })
    input.value = view.getSettings()[key]
    input.addEventListener(
      'change',
      () => {
        view.setSettings({ [key]: input.value })
        dirty = true
      },
      { signal: events.signal },
    )
    field.append(input)
    panel.append(field)
  }
  select('Pipeline', 'pipeline', Nova.rasterPipelines, [
    'Forward',
    'Deferred',
    'Forward+ (2D tiles)',
    'Clustered Forward (3D clusters)',
  ])
  select('Output', 'debug', Nova.pipelineDebugViews, [
    'Final lighting',
    'Base color',
    'World normal',
    'Linear depth',
    'Roughness',
    'Metallic',
    'Light count',
    'List overflow',
  ])
  for (const [key, label, min, max, step] of [
    ['lightCount', 'Lights', 0, 256, 1],
    ['lightRadius', 'Light radius', 0.25, 12, 0.25],
    ['lightIntensity', 'Intensity', 0, 100, 1],
  ]) {
    const field = document.createElement('label'),
      input = document.createElement('input'),
      caption = document.createElement('span')
    const update = () => {
      caption.textContent = `${label}: ${input.value}`
    }
    input.type = 'range'
    input.min = min
    input.max = max
    input.step = step
    input.value = view.getSettings()[key]
    input.setAttribute('aria-label', label)
    update()
    input.addEventListener(
      'input',
      () => {
        view.setSettings({ [key]: Number(input.value) })
        update()
        dirty = true
      },
      { signal: events.signal },
    )
    field.append(caption, input)
    panel.append(field)
  }
  const playback = document.createElement('label'),
    checkbox = document.createElement('input')
  checkbox.type = 'checkbox'
  playback.textContent = 'Continuous benchmark'
  playback.append(checkbox)
  panel.append(playback)
  checkbox.addEventListener(
    'change',
    () => {
      continuous = checkbox.checked
      dirty = true
    },
    { signal: events.signal },
  )
  const orbit = Nova.attachOrbitControls(canvas, {
    getCamera: view.getCamera,
    setCamera: (c) =>
      view.setCamera({
        ...c,
        pitch: Math.max(-80, Math.min(80, c.pitch)),
        distance: Math.max(4, Math.min(50, c.distance)),
      }),
    drawFrame,
    onError: report,
  })
  const resize = new ResizeObserver(() => {
    dirty = true
  })
  resize.observe(canvas)
  async function tick() {
    animation = 0
    if (disposed) return
    try {
      if (visible() && (dirty || continuous)) await drawFrame()
    } catch (e) {
      report(e)
    }
    if (!disposed) animation = requestAnimationFrame(tick)
  }
  await drawFrame()
  animation = requestAnimationFrame(tick)
  return {
    ...view,
    drawFrame,
    dispose() {
      if (disposed) return
      disposed = true
      cancelAnimationFrame(animation)
      events.abort()
      resize.disconnect()
      orbit.dispose()
      bar.dispose()
      view.dispose()
      host.before(canvas)
      host.remove()
    },
  }
}
