// Application UI for the AO example; no graphics API calls.
export function attachAOPanel(canvas, scene, cameras, onError) {
  const host = document.createElement('div')
  host.className = 'ao-viewport'
  canvas.before(host)
  host.append(canvas)
  const style = document.createElement('style')
  style.textContent = `.ao-viewport{width:100%;min-width:0}.ao-controls{margin-top:12px;border:1px solid #3c4659;border-radius:7px;background:#181e29;color:#e1e6f0;font:12px/1.4 system-ui;color-scheme:dark}.ao-controls-body{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(220px,100%),1fr));gap:14px;padding:12px;max-height:360px;overflow:auto}.ao-controls-body[hidden]{display:none}.ao-controls button,.ao-controls select,.ao-controls input{font:inherit;color:inherit}.ao-controls button,.ao-controls select{background:#25324b;border:1px solid #465776;border-radius:4px;padding:5px}.ao-controls select,.ao-controls input{width:100%;box-sizing:border-box}.ao-controls label{display:flex;flex-direction:column;gap:4px;margin:8px 0}.ao-controls input[type=range]{accent-color:#819eff}.ao-controls legend{font-weight:600;color:#b9ccff}.ao-controls fieldset{min-width:0;border:0;padding:0;margin:0}.ao-toggle{display:block;width:100%;text-align:left}`
  host.append(style)
  const panel = document.createElement('section')
  panel.className = 'ao-controls'
  panel.setAttribute('aria-label', 'AO 参数')
  const toggle = document.createElement('button'),
    body = document.createElement('div')
  toggle.className = 'ao-toggle'
  toggle.type = 'button'
  body.className = 'ao-controls-body'
  toggle.textContent = 'Parameters ▾'
  toggle.setAttribute('aria-label', '收起参数面板')
  toggle.setAttribute('aria-expanded', 'true')
  panel.append(toggle, body)
  host.append(panel)
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
      toggle.setAttribute('aria-label', body.hidden ? '显示参数面板' : '收起参数面板')
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
    'AO 算法',
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
    'AO 显示',
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
    ['radius', 'Radius', 'AO 半径', 0.001, 1, 0.001],
    ['bias', 'Bias', 'AO 偏移', 0, 0.05, 0.0005],
    ['strength', 'Strength', 'AO 强度', 0, 4, 0.05],
    ['samples', 'Samples', 'AO 采样数', 4, 64, 4],
    ['resolutionScale', 'Render scale', '渲染比例', 0.25, 1, 0.05],
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
  denoise.setAttribute('aria-label', 'AO 滤波')
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
  const camera = group('Camera')
  let current = 'courtyard'
  select(
    camera,
    'View',
    'AO 视角',
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
  status.setAttribute('aria-label', 'AO 累积')
  camera.append(status)
  fields.push(() => {
    status.textContent = `Frames: ${scene.getAccumulatedSamples()}${scene.getAmbientOcclusion().method === 'rtao' ? ' / 32' : ''}`
  })
  function refresh() {
    fields.forEach((f) => f())
  }
  refresh()
  return {
    refresh,
    dispose() {
      events.abort()
      host.before(canvas)
      host.remove()
    },
  }
}
