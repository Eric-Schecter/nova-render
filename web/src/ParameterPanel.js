// Optional DOM overlay; no framework or WASM dependency.
const dark = [0.025, 0.035, 0.055, 1]
const light = [0.8, 0.85, 0.95, 1]
const styles = `
.nova-viewport{position:relative;width:100%;isolation:isolate}
.nova-panel{position:absolute;z-index:1;top:12px;right:12px;width:250px;max-width:calc(100% - 24px);max-height:calc(100% - 24px);overflow:auto;overscroll-behavior:contain;background:#181e29f5;border:1px solid #3c4659;border-radius:7px;color:#e1e6f0;box-shadow:0 4px 16px #0004;font:12px/1.4 system-ui,sans-serif;text-align:left;color-scheme:dark}
.nova-panel *{box-sizing:border-box}
.nova-panel button,.nova-panel select,.nova-panel input{font:inherit;color:inherit}
.nova-panel button{cursor:pointer;background:#29364e;border:1px solid #465776;border-radius:4px;padding:4px 8px}
.nova-panel button:focus-visible,.nova-panel select:focus-visible,.nova-panel input:focus-visible{outline:2px solid #86a6ff;outline-offset:2px}
.nova-panel .nova-panel-toggle{display:block;width:100%;border:0;border-radius:0;padding:8px 10px;background:#25324b;text-align:left;font-weight:600}
.nova-panel-body{padding:10px}.nova-panel-body[hidden]{display:none}
.nova-panel fieldset{min-width:0;padding:8px 0;margin:0;border:0;border-bottom:1px solid #354056}
.nova-panel fieldset:last-child{border-bottom:0}.nova-panel legend{padding:0;font-weight:600;color:#b9ccff}
.nova-panel label{display:flex;flex-direction:column;gap:3px;margin:6px 0;color:inherit}
.nova-panel .nova-panel-caption{display:flex;justify-content:space-between;gap:8px}
.nova-panel .nova-panel-value{font-variant-numeric:tabular-nums;color:#b4c4de}
.nova-panel input[type=range]{display:block;width:100%;margin:2px 0;accent-color:#819eff}
.nova-panel select{width:100%;background:#25324b;padding:4px;border:1px solid #465776;border-radius:4px}
.nova-panel .nova-panel-rgb{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:7px}
`
const degrees = (value) => (value * 180) / Math.PI
const angles = (direction) => ({
  azimuth: degrees(Math.atan2(direction[0], direction[2])),
  elevation: degrees(Math.atan2(direction[1], Math.hypot(direction[0], direction[2]))),
})
export function attachParameterPanel(canvas, adapter, { onError } = {}) {
  if (!canvas.parentNode) throw Error('Parameter panel requires an attached canvas')
  if (onError != null && typeof onError !== 'function') throw Error('onError must be a function')
  if (!document.getElementById('nova-panel-style')) {
    const style = document.createElement('style')
    style.id = 'nova-panel-style'
    style.textContent = styles
    document.head.append(style)
  }
  const wrapper = document.createElement('div')
  wrapper.className = 'nova-viewport'
  if (canvas.style.width && canvas.style.width !== '100%') {
    wrapper.style.width = canvas.style.width
    wrapper.style.maxWidth = '100%'
  }
  canvas.before(wrapper)
  wrapper.append(canvas)
  const panel = document.createElement('section')
  panel.className = 'nova-panel'
  panel.setAttribute('aria-label', '参数面板')
  const toggle = document.createElement('button')
  toggle.type = 'button'
  toggle.className = 'nova-panel-toggle'
  const body = document.createElement('div')
  body.className = 'nova-panel-body'
  panel.append(toggle, body)
  wrapper.append(panel)
  const events = new AbortController(),
    initialCamera = adapter.getCamera(),
    initialLight = adapter.getLight()
  let disposed = false,
    frame = 0,
    mode = '',
    collapsed = false
  const fields = new Map()
  const report = (error) => {
    if (!disposed) {
      if (onError) onError(error)
      else console.error('[Nova parameter panel]', error)
    }
  }
  function draw() {
    if (frame || disposed) return
    frame = requestAnimationFrame(() => {
      frame = 0
      if (!disposed) Promise.resolve().then(adapter.drawFrame).catch(report)
    })
  }
  function change(action) {
    try {
      action()
      refresh()
      draw()
    } catch (error) {
      report(error)
    }
  }
  function setCollapsed(value) {
    collapsed = value
    body.hidden = value
    toggle.textContent = value ? 'Parameters ▸' : 'Parameters ▾'
    toggle.setAttribute('aria-expanded', String(!value))
    toggle.setAttribute('aria-label', value ? '显示参数面板' : '收起参数面板')
  }
  toggle.addEventListener('click', () => setCollapsed(!collapsed), { signal: events.signal })
  for (const event of ['pointerdown', 'wheel', 'keydown'])
    panel.addEventListener(event, (event) => event.stopPropagation(), { signal: events.signal })
  function group(title) {
    const element = document.createElement('fieldset'),
      legend = document.createElement('legend')
    legend.textContent = title
    element.append(legend)
    body.append(element)
    return element
  }
  function slider(parent, key, caption, label, min, max, step, update) {
    const row = document.createElement('label'),
      heading = document.createElement('span'),
      output = document.createElement('span'),
      input = document.createElement('input')
    heading.className = 'nova-panel-caption'
    output.className = 'nova-panel-value'
    heading.append(caption, output)
    Object.assign(input, { type: 'range', min, max, step })
    input.setAttribute('aria-label', label)
    input.addEventListener('input', () => change(() => update(Number(input.value))), {
      signal: events.signal,
    })
    row.append(heading, input)
    parent.append(row)
    fields.set(key, { input, output })
  }
  function reset(parent, label, action) {
    const button = document.createElement('button')
    button.type = 'button'
    button.textContent = label === '重置相机' ? 'Reset camera' : 'Reset light'
    button.setAttribute('aria-label', label)
    button.addEventListener('click', () => change(action), { signal: events.signal })
    parent.append(button)
  }
  function direction(key, value) {
    const current = adapter.getLight(),
      next = { ...angles(current.direction), [key]: value }
    const a = (next.azimuth * Math.PI) / 180,
      b = (next.elevation * Math.PI) / 180
    adapter.setLight({
      ...current,
      direction: [Math.cos(b) * Math.sin(a), Math.sin(b), Math.cos(b) * Math.cos(a)],
    })
  }
  function build(current) {
    body.replaceChildren()
    fields.clear()
    const scene = group('Scene'),
      label = document.createElement('label'),
      select = document.createElement('select')
    label.textContent = 'Background'
    select.setAttribute('aria-label', '背景色')
    for (const [value, text] of [
      ['dark', 'Dark'],
      ['light', 'Light'],
    ]) {
      const option = document.createElement('option')
      option.value = value
      option.textContent = text
      select.append(option)
    }
    select.addEventListener(
      'change',
      () => change(() => adapter.setClear(select.value === 'light' ? light : dark)),
      { signal: events.signal },
    )
    label.append(select)
    scene.append(label)
    fields.set('background', { input: select })
    if (current.camera) {
      const camera = group('Camera')
      for (const [key, caption, aria, min, max, step] of [
        ['yaw', 'Yaw', '水平角', -180, 180, 0.1],
        ['pitch', 'Pitch', '俯仰角', -85, 85, 0.1],
        ['distance', 'Distance', '距离', 1.5, 100, 0.01],
      ])
        slider(camera, key, caption, aria, min, max, step, (value) =>
          adapter.setCamera({ ...adapter.getCamera(), [key]: value }),
        )
      if (current.lighting)
        slider(camera, 'exposure', 'Exposure', '曝光', 0.1, 3, 0.1, (value) =>
          adapter.setCamera({ ...adapter.getCamera(), exposure: value }),
        )
      reset(camera, '重置相机', () => adapter.setCamera(initialCamera))
    }
    if (current.lighting) {
      const lamp = group('Main light')
      slider(lamp, 'azimuth', 'Azimuth', '光源方位', -180, 180, 0.1, (value) =>
        direction('azimuth', value),
      )
      slider(lamp, 'elevation', 'Elevation', '光源仰角', -90, 90, 0.1, (value) =>
        direction('elevation', value),
      )
      const rgb = document.createElement('div')
      rgb.className = 'nova-panel-rgb'
      lamp.append(rgb)
      for (const [index, name, aria] of [
        [0, 'R', '红'],
        [1, 'G', '绿'],
        [2, 'B', '蓝'],
      ])
        slider(rgb, name, name, '光源' + aria, 0, 1, 0.01, (value) => {
          const light = adapter.getLight()
          light.color[index] = value
          adapter.setLight(light)
        })
      slider(lamp, 'intensity', 'Intensity', '光源强度', 0, 100, 0.1, (value) =>
        adapter.setLight({ ...adapter.getLight(), intensity: value }),
      )
      reset(lamp, '重置光源', () => adapter.setLight(initialLight))
    }
  }
  function refresh() {
    if (disposed) return
    const current = adapter.getMode(),
      signature = JSON.stringify(current)
    if (signature !== mode) {
      mode = signature
      build(current)
    }
    const camera = adapter.getCamera(),
      light = adapter.getLight(),
      lightAngles = angles(light.direction)
    const values = {
      ...camera,
      ...lightAngles,
      R: light.color[0],
      G: light.color[1],
      B: light.color[2],
      intensity: light.intensity,
    }
    for (const [key, field] of fields) {
      if (key === 'background') {
        field.input.value = adapter.getClear()[0] > 0.5 ? 'light' : 'dark'
        continue
      }
      field.input.value = values[key]
      field.output.textContent = Number(values[key]).toFixed(
        key === 'distance' || ['R', 'G', 'B'].includes(key) ? 2 : 1,
      )
    }
  }
  setCollapsed(false)
  refresh()
  return {
    refresh,
    dispose() {
      if (disposed) return
      disposed = true
      events.abort()
      if (frame) cancelAnimationFrame(frame)
      wrapper.before(canvas)
      wrapper.remove()
    },
  }
}
