// Controls specific to this example. No comparison policy lives in Nova's renderer UI.
export function attachComparisonPanel(host, scene, models, onError) {
  const panel = document.createElement('section')
  panel.className = 'comparison-controls'
  panel.setAttribute('aria-label', 'Comparison parameters')
  const toggle = document.createElement('button')
  toggle.type = 'button'
  toggle.className = 'comparison-controls-toggle'
  toggle.textContent = 'Parameters ▾'
  toggle.setAttribute('aria-label', 'Collapse parameters')
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
      toggle.setAttribute('aria-label', body.hidden ? 'Show parameters' : 'Collapse parameters')
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
  model.setAttribute('aria-label', 'Model')
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
    'Comparison sample budget',
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
    'Comparison time budget',
    'number',
    () => scene.getBudget().milliseconds,
    (v) => scene.setBudget({ ...scene.getBudget(), milliseconds: v }),
    0,
    60000,
    1,
  )
  const ray = group('Integrators')
  for (const [key, title, aria, min, max, step] of [
    ['maxBounces', 'Max bounces', 'Maximum bounces', 1, 8, 1],
    ['resolutionScale', 'Render scale', 'Render scale', 0.25, 1, 0.05],
    ['photonsPerIteration', 'Photons / iteration', 'Photons per iteration', 256, 65536, 256],
    ['photonRadius', 'Photon radius', 'Photon radius', 0.03, 0.5, 0.01],
    ['mltBootstrapSamples', 'MLT bootstrap', 'MLT bootstrap samples', 256, 1048576, 256],
    ['mltLargeStepProbability', 'MLT large step', 'MLT large-step probability', 0.05, 1, 0.01],
    ['mltMutationScale', 'MLT mutation scale', 'MLT mutation scale', 0.0001, 0.25, 0.0001],
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
    ['yaw', 'Yaw', 'Yaw', -180, 180, 1],
    ['pitch', 'Pitch', 'Pitch', -85, 85, 1],
    ['distance', 'Distance', 'Distance', 1.5, 100, 0.1],
    ['exposure', 'Exposure', 'Exposure', 0.1, 3, 0.1],
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
  reset.setAttribute('aria-label', 'Reset accumulation')
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
