// Controls specific to this example. No comparison policy lives in Nova's renderer UI.
export function attachComparisonPanel(host, scene, models, onError) {
  const panel = document.createElement('section')
  panel.className = 'comparison-controls'
  panel.setAttribute('aria-label', '对比场景参数')
  const toggle = document.createElement('button')
  toggle.type = 'button'
  toggle.className = 'comparison-controls-toggle'
  toggle.textContent = 'Parameters ▾'
  toggle.setAttribute('aria-label', '收起参数面板')
  toggle.setAttribute('aria-expanded', 'true')
  const body = document.createElement('div')
  body.className = 'comparison-controls-body'
  panel.append(toggle, body)
  host.append(panel)
  const events = new AbortController(),
    fields = []
  const action = (fn) => {
    try {
      Promise.resolve(fn()).catch(onError)
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
  function group(title) {
    const e = document.createElement('fieldset'),
      l = document.createElement('legend')
    l.textContent = title
    e.append(l)
    body.append(e)
    return e
  }
  function input(parent, title, aria, type, get, set, min, max, step) {
    const label = document.createElement('label'),
      caption = document.createElement('span'),
      e = document.createElement('input')
    caption.textContent = title
    e.type = type
    e.min = min
    e.max = max
    e.step = step
    e.setAttribute('aria-label', aria)
    label.append(caption, e)
    parent.append(label)
    e.addEventListener('input', () => action(() => set(Number(e.value))), { signal: events.signal })
    fields.push(() => {
      e.value = get()
      caption.textContent = title + ' · ' + get()
    })
    return e
  }
  const resources = group('Scene'),
    label = document.createElement('label'),
    model = document.createElement('select')
  label.textContent = 'Model'
  model.setAttribute('aria-label', '模型选择')
  models.forEach((choice) => {
    const o = document.createElement('option')
    o.value = choice.url
    o.textContent = choice.name
    model.append(o)
  })
  label.append(model)
  resources.append(label)
  model.addEventListener('change', () => action(() => scene.loadModel(model.value)), {
    signal: events.signal,
  })
  fields.push(() => {
    model.value = scene.getModel()
    model.disabled = scene.isLoading()
  })
  const common = group('Comparison budget')
  const note = document.createElement('p')
  note.textContent = 'Ray tracing · emitter-only · no denoising'
  common.append(note)
  input(
    common,
    'Samples / method',
    '对比采样预算',
    'number',
    () => scene.getBudget().samples,
    (v) => scene.setBudget({ ...scene.getBudget(), samples: v }),
    1,
    65536,
    1,
  )
  input(
    common,
    'Time / method (ms, 0 = samples)',
    '对比时间预算',
    'number',
    () => scene.getBudget().milliseconds,
    (v) => scene.setBudget({ ...scene.getBudget(), milliseconds: v }),
    0,
    60000,
    1,
  )
  const ray = group('Integrators')
  for (const [key, title, aria, min, max, step] of [
    ['maxBounces', 'Max bounces', '反弹次数', 1, 8, 1],
    ['resolutionScale', 'Render scale', '渲染比例', 0.25, 1, 0.05],
    ['photonsPerIteration', 'Photons / iteration', '每轮光子数', 256, 65536, 256],
    ['photonRadius', 'Photon radius', '光子收集半径', 0.03, 0.5, 0.01],
    ['mltBootstrapSamples', 'MLT bootstrap', 'MLT 预采样数', 256, 1048576, 256],
    ['mltLargeStepProbability', 'MLT large step', 'MLT 大步概率', 0.05, 1, 0.01],
    ['mltMutationScale', 'MLT mutation scale', 'MLT 小步幅度', 0.0001, 0.25, 0.0001],
  ])
    input(
      ray,
      title,
      aria,
      'range',
      () => scene.getRayTracing()[key],
      (v) => scene.setRayTracing({ ...scene.getRayTracing(), [key]: v }),
      min,
      max,
      step,
    )
  const camera = group('Camera')
  for (const [key, title, aria, min, max, step] of [
    ['yaw', 'Yaw', '水平角', -180, 180, 1],
    ['pitch', 'Pitch', '俯仰角', -85, 85, 1],
    ['distance', 'Distance', '距离', 1.5, 100, 0.1],
    ['exposure', 'Exposure', '曝光', 0.1, 3, 0.1],
  ])
    input(
      camera,
      title,
      aria,
      'range',
      () => scene.getCamera()[key],
      (v) => scene.setCamera({ ...scene.getCamera(), [key]: v }),
      min,
      max,
      step,
    )
  const reset = document.createElement('button')
  reset.type = 'button'
  reset.textContent = 'Reset accumulation'
  reset.setAttribute('aria-label', '重置累计')
  common.append(reset)
  reset.addEventListener('click', () => action(() => scene.resetAccumulation()), {
    signal: events.signal,
  })
  function refresh() {
    fields.forEach((f) => f())
  }
  refresh()
  return {
    refresh,
    dispose() {
      events.abort()
      panel.remove()
    },
  }
}
