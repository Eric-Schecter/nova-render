import * as Nova from '../src/Nova.js'
import { createFrameMeter } from './FrameMeter.js'
import { attachPerformanceBar } from './PerformancePanel.js'

export async function showRasterLaboratory(
  canvas,
  shaderDirectory = '/generated/',
  {
    onError,
    createView = Nova.createAntiAliasingView,
    initialPlaying = true,
    showChecker = true,
    extraControls,
    panelLabel = '抗锯齿参数',
    describe = () => '',
  } = {},
) {
  const view = await createView({ canvas, shaderDirectory }),
    meter = createFrameMeter()
  const host = document.createElement('div')
  host.className = 'aa-laboratory'
  canvas.before(host)
  host.append(canvas)
  const style = document.createElement('style')
  style.textContent = `.aa-laboratory{width:100%;min-width:0}.aa-laboratory canvas{display:block;width:100%;aspect-ratio:1.65;height:auto;image-rendering:auto}.aa-parameters{margin-top:12px;padding:12px;background:#181e29;border:1px solid #3c4659;border-radius:7px;color:#e1e6f0;font:12px/1.4 system-ui;color-scheme:dark;display:grid;grid-template-columns:repeat(auto-fit,minmax(min(180px,100%),1fr));gap:12px}.aa-parameters label{display:flex;flex-direction:column;gap:5px;min-width:0}.aa-parameters select,.aa-parameters input,.aa-parameters button{font:inherit;box-sizing:border-box;width:100%;background:#25324b;color:inherit;border:1px solid #465776;border-radius:4px;padding:5px}.aa-parameters input[type=checkbox]{width:auto;align-self:flex-start}.aa-parameters input[type=range]{accent-color:#819eff}.aa-note{grid-column:1/-1;color:#b9ccff}`
  host.append(style)
  let playing = initialPlaying,
    scale = 0.5,
    speed = 0.35,
    time = 0,
    frame = 0,
    busy = false,
    dirty = true,
    disposed = false,
    lastTime,
    settle = 32,
    failure = false
  let oldWidth = 0,
    oldHeight = 0
  const events = new AbortController(),
    panel = document.createElement('section')
  panel.className = 'aa-parameters'
  panel.setAttribute('aria-label', panelLabel)
  host.append(panel)
  const fields = []
  const methods = [
    ['off', 'Off'],
    ['fxaa', 'FXAA'],
    ['smaa', 'SMAA 1x'],
    ['msaa', 'MSAA 4x'],
    ['ssaa', 'SSAA 2×2'],
    ['taa', 'TAA'],
  ]
  const visible = () => {
    const r = canvas.getBoundingClientRect()
    return (
      !document.hidden &&
      !host.closest('[inert],[aria-hidden="true"]') &&
      r.width > 0 &&
      r.height > 0 &&
      r.bottom > 0 &&
      r.top < innerHeight
    )
  }
  const stats = () => ({
    ...meter.snapshot(
      !visible()
        ? 'paused'
        : busy
          ? 'rendering'
          : playing
            ? 'rendering'
            : view.getAntiAliasing().method === 'taa' && settle > 0
              ? 'accumulating'
              : 'idle',
    ),
    accumulatedFrames: view.getHistoryFrames(),
  })
  const bar = attachPerformanceBar(host, stats)
  const report = (e) => {
    failure = true
    onError?.(e)
    canvas.dispatchEvent(new CustomEvent('nova-error', { detail: e }))
  }
  function schedule() {
    if (frame || disposed || failure) return
    if (dirty || playing || (view.getAntiAliasing().method === 'taa' && settle > 0))
      frame = requestAnimationFrame(() => {
        frame = 0
        controller.drawFrame().catch(report)
      })
  }
  function changed() {
    dirty = true
    settle = 32
    fields.forEach((f) => f())
    schedule()
  }
  function control(label, aria, kind, get, set, choices, enabled = () => true) {
    const wrapper = document.createElement('label'),
      title = document.createElement('span'),
      input = document.createElement(kind === 'select' ? 'select' : 'input')
    title.textContent = label
    input.setAttribute('aria-label', aria)
    if (kind !== 'select') input.type = kind
    if (choices && kind === 'select')
      for (const [value, name] of choices) {
        const option = document.createElement('option')
        option.value = value
        option.textContent = name
        input.append(option)
      }
    if (kind === 'range') {
      input.min = choices[0]
      input.max = choices[1]
      input.step = choices[2]
    }
    fields.push(() => {
      if (aria === '历史权重') input.disabled = view.getAntiAliasing().method !== 'taa'
      if (kind === 'checkbox') input.checked = get()
      else input.value = String(get())
      input.disabled = !enabled()
      if (kind === 'range')
        title.textContent =
          label +
          ' · ' +
          Number(get()).toFixed(
            Math.min(4, Math.max(0, Math.ceil(-Math.log10(Number(choices?.[2] || 0.01))))),
          )
    })
    input.addEventListener(
      kind === 'range' ? 'input' : 'change',
      () => {
        try {
          set(
            kind === 'checkbox'
              ? input.checked
              : kind === 'select'
                ? input.value
                : Number(input.value),
          )
          changed()
        } catch (e) {
          report(e)
        }
      },
      { signal: events.signal },
    )
    wrapper.append(title, input)
    panel.append(wrapper)
  }
  const controller = {
    getAntiAliasing: () => view.getAntiAliasing(),
    setAntiAliasing(value) {
      view.setAntiAliasing(value)
      changed()
    },
    getCamera: () => view.getCamera(),
    setCamera(value) {
      view.setCamera(value)
      changed()
    },
    getPerformanceStats: stats,
    getPlaying: () => playing,
    setPlaying(value) {
      playing = !!value
      lastTime = undefined
      changed()
    },
    getHistoryFrames: () => view.getHistoryFrames(),
    resetHistory() {
      view.resetHistory()
      changed()
    },
    async drawFrame() {
      if (disposed) throw Error('AA example disposed')
      if (busy) {
        dirty = true
        return 'cached'
      }
      if (!visible()) {
        lastTime = undefined
        return 'skipped'
      }
      const r = canvas.getBoundingClientRect(),
        pixelW = r.width * devicePixelRatio * scale,
        pixelH = r.height * devicePixelRatio * scale,
        limit = Math.min(1, 1024 / Math.max(pixelW, pixelH)),
        w = Math.max(1, Math.round(pixelW * limit)),
        h = Math.max(1, Math.round(pixelH * limit))
      if (w !== oldWidth || h !== oldHeight) {
        oldWidth = w
        oldHeight = h
        dirty = true
        settle = 32
      }
      if (!dirty && !playing && !(view.getAntiAliasing().method === 'taa' && settle > 0))
        return 'cached'
      busy = true
      dirty = false
      const start = performance.now()
      try {
        const delta = lastTime == null ? 0 : Math.min(0.1, (start - lastTime) / 1000)
        if (playing) time += delta * speed
        lastTime = start
        view.setTime(time)
        await view.drawFrame(w, h)
        settle = Math.max(0, settle - 1)
        const method = view.getAntiAliasing().method,
          ss = method === 'ssaa' ? 2 : 1
        meter.record(start, {
          width: w * ss,
          height: h * ss,
          scale: scale * limit * ss,
          samples: method === 'msaa' || method === 'ssaa' ? 4 : 1,
          method: describe(view) + methods.find((m) => m[0] === method)[1],
        })
        return 'rendered'
      } finally {
        busy = false
        if (!disposed) {
          bar.refresh()
          schedule()
        }
      }
    },
    dispose() {
      if (disposed) return
      disposed = true
      if (frame) cancelAnimationFrame(frame)
      events.abort()
      observer.disconnect()
      resizeObserver.disconnect()
      orbit.dispose()
      bar.dispose()
      view.dispose()
      host.before(canvas)
      host.remove()
    },
  }
  extraControls?.({ view, control, controller, changed })
  control(
    'Method',
    '抗锯齿算法',
    'select',
    () => view.getAntiAliasing().method,
    (v) => controller.setAntiAliasing({ method: v }),
    methods,
  )
  if (showChecker)
    control(
      'Texture filtering',
      '棋盘过滤',
      'checkbox',
      () => view.getAntiAliasing().textureFiltering,
      (v) => controller.setAntiAliasing({ textureFiltering: v }),
    )
  control(
    'Animate',
    '播放运动',
    'checkbox',
    () => playing,
    (v) => controller.setPlaying(v),
  )
  control(
    'Speed',
    '运动速度',
    'range',
    () => speed,
    (v) => {
      speed = v
    },
    [0.05, 1, 0.05],
  )
  control(
    'Split: Off | selected AA',
    '分割对比',
    'checkbox',
    () => view.getAntiAliasing().split,
    (v) => controller.setAntiAliasing({ split: v }),
  )
  control(
    'Zoom',
    '局部放大',
    'select',
    () => view.getAntiAliasing().zoom,
    (v) => controller.setAntiAliasing({ zoom: Number(v) }),
    [
      ['1', '1×'],
      ['2', '2×'],
      ['4', '4×'],
      ['8', '8×'],
    ],
  )
  control(
    'Render scale',
    '渲染比例',
    'range',
    () => scale,
    (v) => {
      scale = v
      view.resetHistory()
    },
    [0.25, 1, 0.25],
  )
  control(
    'TAA history weight',
    '历史权重',
    'range',
    () => view.getAntiAliasing().historyWeight,
    (v) => controller.setAntiAliasing({ historyWeight: v }),
    [0, 0.98, 0.01],
  )
  const reset = document.createElement('button')
  reset.textContent = 'Reset history'
  reset.setAttribute('aria-label', '重置历史帧')
  reset.addEventListener('click', () => controller.resetHistory(), { signal: events.signal })
  panel.append(reset)
  const note = document.createElement('div')
  note.className = 'aa-note'
  note.textContent = 'Split：左侧 Off，右侧所选 AA。拖动旋转，滚轮缩放。'
  panel.append(note)
  const orbit = Nova.attachOrbitControls(canvas, {
    getCamera: () => controller.getCamera(),
    setCamera: (c) =>
      controller.setCamera({
        ...c,
        distance: Math.max(4, Math.min(20, c.distance)),
        pitch: Math.max(-60, Math.min(60, c.pitch)),
      }),
    drawFrame: () => controller.drawFrame(),
    onError: report,
  })
  const observer = new IntersectionObserver(() => {
      lastTime = undefined
      schedule()
    }),
    resizeObserver = new ResizeObserver(changed)
  observer.observe(canvas)
  resizeObserver.observe(canvas)
  document.addEventListener(
    'visibilitychange',
    () => {
      lastTime = undefined
      schedule()
    },
    { signal: events.signal },
  )
  fields.push(() => {
    reset.disabled = view.getAntiAliasing().method !== 'taa'
  })
  fields.forEach((f) => f())
  try {
    await controller.drawFrame()
    return controller
  } catch (error) {
    controller.dispose()
    throw error
  }
}
