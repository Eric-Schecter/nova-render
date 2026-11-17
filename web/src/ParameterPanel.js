import { gBufferViews } from './RasterPipeline.js'
// Optional single-view DOM overlay.
const dark = [0.025, 0.035, 0.055, 1]
const light = [0.8, 0.85, 0.95, 1]
const styles = `
.nova-viewport{position:relative;width:100%;isolation:isolate}
.nova-render-surface{position:relative;width:100%;min-width:0}
.nova-panel{position:absolute;z-index:1;top:12px;right:12px;width:250px;max-width:calc(100% - 24px);max-height:calc(100% - 24px);overflow:auto;overscroll-behavior:contain;background:#181e29f5;border:1px solid #3c4659;border-radius:7px;color:#e1e6f0;box-shadow:0 4px 16px #0004;font:12px/1.4 system-ui,sans-serif;text-align:left;color-scheme:dark}
.nova-panel *{box-sizing:border-box}
.nova-panel button,.nova-panel select,.nova-panel input{font:inherit;color:inherit}
.nova-panel button{cursor:pointer;background:#29364e;border:1px solid #465776;border-radius:4px;padding:4px 8px}
.nova-panel button:focus-visible,.nova-panel select:focus-visible,.nova-panel input:focus-visible{outline:2px solid #86a6ff;outline-offset:2px}
.nova-panel .nova-panel-toggle{position:sticky;top:0;z-index:1;display:block;width:100%;border:0;border-radius:0;padding:8px 10px;background:#25324b;text-align:left;font-weight:600}
.nova-panel-body{padding:10px}.nova-panel-body[hidden]{display:none}
.nova-panel fieldset{min-width:0;padding:8px 0;margin:0;border:0;border-bottom:1px solid #354056}
.nova-panel fieldset:last-child{border-bottom:0}.nova-panel legend{padding:0;font-weight:600;color:#b9ccff}
.nova-panel label{display:flex;flex-direction:column;gap:3px;margin:6px 0;color:inherit}
.nova-panel .nova-panel-caption{display:flex;justify-content:space-between;gap:8px}
.nova-panel .nova-panel-value{font-variant-numeric:tabular-nums;color:#b4c4de}
.nova-panel input[type=range]{display:block;width:100%;margin:2px 0;accent-color:#819eff}
.nova-panel select{width:100%;background:#25324b;padding:4px;border:1px solid #465776;border-radius:4px}
.nova-panel .nova-panel-check{flex-direction:row;align-items:center;gap:6px}
.nova-panel select:disabled{opacity:.5}
.nova-panel .nova-panel-rgb{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:7px}
`
const degrees = (value) => (value * 180) / Math.PI
const angles = (direction) => ({
  azimuth: degrees(Math.atan2(direction[0], direction[2])),
  elevation: degrees(Math.atan2(direction[1], Math.hypot(direction[0], direction[2]))),
})
export function attachParameterPanel(
  canvas,
  adapter,
  { onError, models = [], environments = [] } = {},
) {
  const copyChoices = (choices) => {
    if (!Array.isArray(choices)) throw Error('Asset choices must be arrays')
    return choices.map((choice) => {
      if (
        !choice ||
        typeof choice.name !== 'string' ||
        !choice.name.trim() ||
        typeof choice.url !== 'string' ||
        !choice.url.trim()
      )
        throw Error('Asset choices require a name and URL')
      return { name: choice.name, url: choice.url }
    })
  }
  models = copyChoices(models)
  environments = copyChoices(environments)
  if (!canvas.parentNode) throw Error('Parameter panel requires an attached canvas')
  if (onError != null && typeof onError !== 'function') throw Error('onError must be a function')
  let style = document.getElementById('nova-panel-style')
  if (!style) {
    style = document.createElement('style')
    style.id = 'nova-panel-style'
    document.head.append(style)
  }
  if (style.textContent !== styles) style.textContent = styles
  const wrapper = document.createElement('div')
  wrapper.className = 'nova-viewport'
  if (canvas.style.width && canvas.style.width !== '100%') {
    wrapper.style.width = canvas.style.width
    wrapper.style.maxWidth = '100%'
  }
  canvas.before(wrapper)
  const surface = document.createElement('div')
  surface.className = 'nova-render-surface'
  wrapper.append(surface)
  surface.append(canvas)
  const panel = document.createElement('section')
  panel.className = 'nova-panel'
  panel.setAttribute('aria-label', 'Parameters')
  const toggle = document.createElement('button')
  toggle.type = 'button'
  toggle.className = 'nova-panel-toggle'
  const body = document.createElement('div')
  body.className = 'nova-panel-body'
  panel.append(toggle, body)
  wrapper.append(panel)
  const events = new AbortController(),
    initialCamera = adapter.getCamera(),
    initialLight = adapter.getLight(),
    initialEnvironment = adapter.getEnvironment()
  let disposed = false,
    frame = 0,
    mode = '',
    collapsed = false,
    loading = false
  let fieldEvents = new AbortController()
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
    toggle.setAttribute('aria-label', value ? 'Show parameters' : 'Collapse parameters')
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
      signal: fieldEvents.signal,
    })
    row.append(heading, input)
    parent.append(row)
    fields.set(key, { input, output })
  }
  function reset(parent, label, action) {
    const button = document.createElement('button')
    button.type = 'button'
    button.textContent =
      label === 'Reset accumulation'
        ? 'Reset accumulation'
        : label === 'Reset camera'
          ? 'Reset camera'
          : label === 'Reset environment'
            ? 'Reset environment'
            : 'Reset light'
    button.setAttribute('aria-label', label)
    button.addEventListener('click', () => change(action), { signal: fieldEvents.signal })
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
  function assetSelect(parent, key, caption, aria, choices, load) {
    if (!choices.length) return
    const label = document.createElement('label'),
      input = document.createElement('select')
    label.textContent = caption
    input.setAttribute('aria-label', aria)
    for (const choice of choices) {
      const option = document.createElement('option')
      option.value = choice.url
      option.textContent = choice.name
      input.append(option)
    }
    label.append(input)
    parent.append(label)
    fields.set(key, { input, asset: true })
    input.addEventListener(
      'change',
      async () => {
        if (loading || disposed) return
        const url = input.value
        loading = true
        refresh()
        try {
          await load(url)
          if (!disposed) draw()
        } catch (error) {
          report(error)
        } finally {
          loading = false
          refresh()
        }
      },
      { signal: fieldEvents.signal },
    )
  }
  function build(current) {
    fieldEvents.abort()
    fieldEvents = new AbortController()
    body.replaceChildren()
    fields.clear()
    if (adapter.getRasterSettings && current.camera) {
      const renderer = group('Raster pipeline')
      for (const [key, label, values, labels] of [
        ['pipeline', 'Raster pipeline', ['forward', 'deferred'], ['Forward', 'Deferred']],
        [
          'view',
          'G-buffer view',
          gBufferViews,
          [
            'Final lighting',
            'Base color',
            'World normal',
            'Geometric normal',
            'Linear depth',
            'Roughness',
            'Metallic',
            'Occlusion',
            'Emissive',
            'World position',
          ],
        ],
      ]) {
        const field = document.createElement('label'),
          input = document.createElement('select')
        field.textContent = label
        input.setAttribute('aria-label', label)
        values.forEach((v, i) => {
          const o = document.createElement('option')
          o.value = v
          o.textContent = labels[i]
          input.append(o)
        })
        input.addEventListener(
          'change',
          () =>
            change(() =>
              adapter.setRasterSettings(
                key === 'pipeline'
                  ? { pipeline: input.value, view: 'final' }
                  : { view: input.value },
              ),
            ),
          { signal: fieldEvents.signal },
        )
        field.append(input)
        renderer.append(field)
        fields.set('raster_' + key, { input })
      }
      slider(renderer, 'raster_debugRange', 'Debug range', 'Debug range', 0.1, 200, 0.1, (v) =>
        adapter.setRasterSettings({ debugRange: v }),
      )
      const note = document.createElement('small')
      note.textContent = 'Deferred: no MSAA. Diagnostics bypass AA and post-processing.'
      renderer.append(note)
    }
    if (adapter.getAntiAliasing) {
      const renderer = group('Anti-aliasing'),
        label = document.createElement('label'),
        select = document.createElement('select')
      label.textContent = 'Algorithm'
      select.setAttribute('aria-label', 'Anti-aliasing method')
      for (const [value, text] of [
        ['off', 'Off'],
        ['fxaa', 'FXAA'],
        ['smaa', 'SMAA 1x'],
        ['msaa', 'MSAA 4x'],
        ['ssaa', 'SSAA 2×2'],
        ['taa', 'TAA'],
      ]) {
        const option = document.createElement('option')
        option.value = value
        option.textContent = text
        select.append(option)
      }
      select.addEventListener(
        'change',
        () => change(() => adapter.setAntiAliasing({ method: select.value })),
        { signal: fieldEvents.signal },
      )
      label.append(select)
      renderer.append(label)
      fields.set('aaMethod', { input: select })
    }
    if (current.camera) {
      const renderer = group('Renderer'),
        label = document.createElement('label'),
        select = document.createElement('select')
      label.textContent = 'Mode'
      select.setAttribute('aria-label', 'Render mode')
      const capabilities = adapter.getCapabilities()
      for (const [value, text] of [
        ['raster', 'Raster'],
        ['ray-tracing', 'Ray tracing'],
        ['hybrid', 'Hybrid'],
      ]) {
        const option = document.createElement('option')
        option.value = value
        option.textContent = text
        if (value === 'ray-tracing' && !capabilities.pathTracing) {
          option.disabled = true
          option.title = capabilities.reason
        }
        if (value === 'hybrid') {
          const caps = adapter.getHybridCapabilities?.(),
            aa = adapter.getAntiAliasing()
          option.disabled =
            !caps?.supported ||
            ['msaa', 'taa'].includes(aa.method) ||
            aa.split ||
            !!adapter.getAmbientOcclusion?.().enabled
          option.title = caps?.reason || ''
        }
        select.append(option)
      }
      select.value = adapter.getRenderMode()
      select.title = capabilities.reason
      select.addEventListener('change', () => change(() => adapter.setRenderMode(select.value)), {
        signal: fieldEvents.signal,
      })
      label.append(select)
      renderer.append(label)
      fields.set('renderMode', { input: select })
      if (adapter.getRenderMode() === 'hybrid' && adapter.getHybrid) {
        const h = adapter.getHybrid(),
          row = document.createElement('label'),
          toggle = document.createElement('input')
        row.textContent = 'Hybrid shadows'
        toggle.type = 'checkbox'
        toggle.checked = h.shadows
        toggle.setAttribute('aria-label', 'Hybrid shadows')
        toggle.addEventListener(
          'change',
          () => change(() => adapter.setHybrid({ shadows: toggle.checked })),
          { signal: fieldEvents.signal },
        )
        row.prepend(toggle)
        renderer.append(row)
        fields.set('hybrid_shadows', { input: toggle })
        slider(
          renderer,
          'hybrid_shadowSamples',
          'Shadow rays/pixel/light',
          'Shadow rays per pixel per light',
          1,
          16,
          1,
          (v) => adapter.setHybrid({ shadowSamples: v }),
        )
        const aoRow = document.createElement('label'),
          aoToggle = document.createElement('input')
        aoRow.textContent = 'Hybrid ambient occlusion'
        aoToggle.type = 'checkbox'
        aoToggle.setAttribute('aria-label', 'Hybrid ambient occlusion')
        aoToggle.checked = h.ambientOcclusion
        aoToggle.addEventListener(
          'change',
          () => change(() => adapter.setHybrid({ ambientOcclusion: aoToggle.checked })),
          { signal: fieldEvents.signal },
        )
        aoRow.prepend(aoToggle)
        renderer.append(aoRow)
        fields.set('hybrid_ambientOcclusion', { input: aoToggle })
        slider(renderer, 'hybrid_aoRadius', 'AO radius', 'AO radius', 0.01, 10, 0.01, (v) =>
          adapter.setHybrid({ aoRadius: v }),
        )
        slider(renderer, 'hybrid_aoStrength', 'AO strength', 'AO strength', 0, 4, 0.05, (v) =>
          adapter.setHybrid({ aoStrength: v }),
        )
        slider(renderer, 'hybrid_aoSamples', 'AO rays/pixel', 'AO rays per pixel', 1, 32, 1, (v) =>
          adapter.setHybrid({ aoSamples: v }),
        )
        const viewRow = document.createElement('label'),
          view = document.createElement('select')
        viewRow.textContent = 'Hybrid view'
        view.setAttribute('aria-label', 'Hybrid view')
        for (const value of ['final', 'shadows', 'ambient-occlusion']) {
          const option = document.createElement('option')
          option.value = value
          option.textContent =
            value === 'final' ? 'Final' : value === 'shadows' ? 'Shadows' : 'Ambient occlusion'
          view.append(option)
        }
        view.value = h.view
        view.addEventListener(
          'change',
          () => change(() => adapter.setHybrid({ view: view.value })),
          { signal: fieldEvents.signal },
        )
        viewRow.append(view)
        renderer.append(viewRow)
        fields.set('hybrid_view', { input: view })
        slider(
          renderer,
          'hybrid_shadowLight',
          'Visibility light',
          'Visibility light',
          0,
          15,
          1,
          (v) => adapter.setHybrid({ shadowLight: v }),
        )
      }
      if (current.pathTracing) {
        const integratorRow = document.createElement('label'),
          integrator = document.createElement('select')
        integratorRow.textContent = 'Integrator'
        integrator.setAttribute('aria-label', 'Path integrator')
        for (const [value, title] of [
          ['path', 'Path tracing'],
          ['bidirectional', 'Bidirectional'],
          ['photon-mapping', 'Photon mapping'],
          ['mlt', 'MLT'],
        ]) {
          const option = document.createElement('option')
          option.value = value
          option.textContent = title
          integrator.append(option)
        }
        integratorRow.append(integrator)
        renderer.append(integratorRow)
        integrator.addEventListener(
          'change',
          () =>
            change(() =>
              adapter.setPathTracing({ ...adapter.getPathTracing(), integrator: integrator.value }),
            ),
          { signal: fieldEvents.signal },
        )
        fields.set('integrator', { input: integrator })

        for (const [key, caption, aria, min, max, step] of [
          ['samplesPerFrame', 'Samples per frame', 'Samples per frame', 1, 4, 1],
          ['maxBounces', 'Max bounces', 'Maximum bounces', 1, 8, 1],
          ['resolutionScale', 'Render scale', 'Render scale', 0.25, 1, 0.05],
          [
            'mltBootstrapSamples',
            'MLT bootstrap samples',
            'MLT bootstrap samples',
            256,
            1048576,
            256,
          ],
          [
            'mltLargeStepProbability',
            'MLT large-step probability',
            'MLT large-step probability',
            0.05,
            1,
            0.01,
          ],
          ['mltMutationScale', 'MLT mutation scale', 'MLT mutation scale', 0.0001, 0.25, 0.0001],
          [
            'photonsPerIteration',
            'Photons per iteration',
            'Photons per iteration',
            256,
            65536,
            256,
          ],
          ['photonRadius', 'Initial gather radius', 'Photon gather radius', 0.03, 0.5, 0.005],
        ])
          slider(renderer, key, caption, aria, min, max, step, (value) =>
            adapter.setPathTracing({ ...adapter.getPathTracing(), [key]: value }),
          )
        const denoiseRow = document.createElement('label'),
          denoise = document.createElement('input')
        denoiseRow.className = 'nova-panel-check'
        denoise.type = 'checkbox'
        denoise.setAttribute('aria-label', 'Denoise')
        denoiseRow.append(denoise, 'Denoise')
        renderer.append(denoiseRow)
        denoise.addEventListener(
          'change',
          () =>
            change(() =>
              adapter.setPathTracing({ ...adapter.getPathTracing(), denoise: denoise.checked }),
            ),
          { signal: fieldEvents.signal },
        )
        fields.set('denoise', { input: denoise })
        const samplingRow = document.createElement('label'),
          sampling = document.createElement('input')
        samplingRow.className = 'nova-panel-check'
        sampling.type = 'checkbox'
        sampling.setAttribute('aria-label', 'Light sampling and MIS')
        samplingRow.append(sampling, 'Light sampling / MIS')
        renderer.append(samplingRow)
        sampling.addEventListener(
          'change',
          () =>
            change(() =>
              adapter.setPathTracing({
                ...adapter.getPathTracing(),
                lightSampling: sampling.checked,
              }),
            ),
          { signal: fieldEvents.signal },
        )
        fields.set('lightSampling', { input: sampling })

        const samples = document.createElement('div')
        samples.setAttribute('aria-label', 'Accumulated samples')
        renderer.append(samples)
        fields.set('samples', { output: samples })
        const method = document.createElement('div')
        method.textContent = 'WebGPU compute BVH'
        renderer.append(method)
        reset(renderer, 'Reset accumulation', () => adapter.resetAccumulation())
      }
    }
    const clips = adapter.getAnimations()
    if (clips.length) {
      const animation = group('Animation'),
        label = document.createElement('label'),
        select = document.createElement('select')
      label.textContent = 'Clip'
      select.setAttribute('aria-label', 'Animation clip')
      clips.forEach((clip, i) => {
        const option = document.createElement('option')
        option.value = i
        option.textContent = clip.name
        select.append(option)
      })
      select.addEventListener(
        'change',
        () =>
          change(() =>
            adapter.setAnimationState({
              ...adapter.getAnimationState(),
              clip: Number(select.value),
              time: 0,
            }),
          ),
        { signal: fieldEvents.signal },
      )
      label.append(select)
      animation.append(label)
      fields.set('animationClip', { input: select })
      for (const [key, text, aria] of [
        ['playing', 'Playing', 'Play animation'],
        ['loop', 'Loop', 'Loop animation'],
      ]) {
        const row = document.createElement('label'),
          input = document.createElement('input')
        row.className = 'nova-panel-check'
        input.type = 'checkbox'
        input.setAttribute('aria-label', aria)
        input.addEventListener(
          'change',
          () =>
            change(() =>
              adapter.setAnimationState({ ...adapter.getAnimationState(), [key]: input.checked }),
            ),
          { signal: fieldEvents.signal },
        )
        row.append(input, text)
        animation.append(row)
        fields.set('animation_' + key, { input })
      }
      slider(animation, 'animationSpeed', 'Speed', 'Animation speed', 0.1, 4, 0.1, (value) =>
        adapter.setAnimationState({ ...adapter.getAnimationState(), speed: value }),
      )
      slider(
        animation,
        'animationTime',
        'Time',
        'Animation time',
        0,
        clips[adapter.getAnimationState().clip].duration,
        0.01,
        (value) => adapter.setAnimationState({ ...adapter.getAnimationState(), time: value }),
      )
    }
    if (models.length) {
      const assets = group('Model')
      assetSelect(assets, 'model', 'Model', 'Model', models, adapter.loadModel)
    }
    const scene = group('Scene'),
      label = document.createElement('label'),
      select = document.createElement('select')
    label.textContent = 'Background'
    select.setAttribute('aria-label', 'Clear color')
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
      { signal: fieldEvents.signal },
    )
    label.append(select)
    scene.append(label)
    fields.set('background', { input: select })
    if (current.camera) {
      const camera = group('Camera')
      for (const [key, caption, aria, min, max, step] of [
        ['yaw', 'Yaw', 'Yaw', -180, 180, 0.1],
        ['pitch', 'Pitch', 'Pitch', -85, 85, 0.1],
        ['distance', 'Distance', 'Distance', 1.5, 100, 0.01],
      ])
        slider(camera, key, caption, aria, min, max, step, (value) =>
          adapter.setCamera({ ...adapter.getCamera(), [key]: value }),
        )
      if (current.lighting || current.environment)
        slider(camera, 'exposure', 'Exposure', 'Exposure', 0.1, 16, 0.1, (value) =>
          adapter.setCamera({ ...adapter.getCamera(), exposure: value }),
        )
      reset(camera, 'Reset camera', () => adapter.setCamera(initialCamera))
    }
    if (current.environment) {
      const environment = group('Environment'),
        checkboxRow = document.createElement('label'),
        checkbox = document.createElement('input')
      assetSelect(
        environment,
        'environment',
        'Map',
        'Environment map',
        environments,
        adapter.loadEnvironment,
      )
      checkboxRow.className = 'nova-panel-check'
      checkbox.type = 'checkbox'
      checkbox.setAttribute('aria-label', 'Show skybox')
      checkboxRow.append(checkbox, 'Skybox')
      environment.append(checkboxRow)
      checkbox.addEventListener(
        'change',
        () =>
          change(() =>
            adapter.setEnvironment({
              ...adapter.getEnvironment(),
              showBackground: checkbox.checked,
            }),
          ),
        { signal: fieldEvents.signal },
      )
      fields.set('showBackground', { input: checkbox })
      for (const [key, caption, aria, min, max, step] of [
        ['rotation', 'Rotation', 'Environment rotation', -180, 180, 0.1],
        ['environmentIntensity', 'Intensity', 'Environment intensity', 0, 16, 0.01],
        ['backgroundIntensity', 'Sky brightness', 'Skybox intensity', 0, 16, 0.01],
      ])
        slider(environment, key, caption, aria, min, max, step, (value) =>
          adapter.setEnvironment({
            ...adapter.getEnvironment(),
            [key === 'environmentIntensity' ? 'intensity' : key]: value,
          }),
        )
      reset(environment, 'Reset environment', () => adapter.setEnvironment(initialEnvironment))
    }
    if (current.lighting) {
      const lamp = group('Main light')
      slider(lamp, 'azimuth', 'Azimuth', 'Light azimuth', -180, 180, 0.1, (value) =>
        direction('azimuth', value),
      )
      slider(lamp, 'elevation', 'Elevation', 'Light elevation', -90, 90, 0.1, (value) =>
        direction('elevation', value),
      )
      const rgb = document.createElement('div')
      rgb.className = 'nova-panel-rgb'
      lamp.append(rgb)
      for (const [index, name, aria] of [
        [0, 'R', 'Red'],
        [1, 'G', 'Green'],
        [2, 'B', 'Blue'],
      ])
        slider(rgb, name, name, 'Light ' + aria, 0, 1, 0.01, (value) => {
          const light = adapter.getLight()
          light.color[index] = value
          adapter.setLight(light)
        })
      slider(lamp, 'intensity', 'Intensity', 'Light intensity', 0, 100, 0.1, (value) =>
        adapter.setLight({ ...adapter.getLight(), intensity: value }),
      )
      reset(lamp, 'Reset light', () => adapter.setLight(initialLight))
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
    const environment = adapter.getEnvironment()
    const values = {
      rotation: environment.rotation,
      environmentIntensity: environment.intensity,
      backgroundIntensity: environment.backgroundIntensity,
      ...camera,
      ...adapter.getPathTracing(),
      ...lightAngles,
      R: light.color[0],
      G: light.color[1],
      B: light.color[2],
      intensity: light.intensity,
    }
    const assets = adapter.getAssets()
    for (const [key, field] of fields) {
      if (key.startsWith('hybrid_')) {
        const name = key.slice(7),
          value = adapter.getHybrid()[name]
        if (typeof value === 'boolean') field.input.checked = value
        else field.input.value = value
        if (field.output) field.output.textContent = String(value)
        if (name === 'shadowLight')
          field.input.closest('label').style.display =
            adapter.getHybrid().view === 'shadows' ? '' : 'none'
        continue
      }
      if (key.startsWith('raster_')) {
        const keyName = key.slice(7),
          raster = adapter.getRasterSettings()
        field.input.value = raster[keyName]
        if (keyName === 'debugRange') {
          field.output.textContent = Number(raster.debugRange).toFixed(1)
          field.input.closest('label').style.display = raster.view === 'final' ? 'none' : ''
        }
        field.input.disabled =
          adapter.getRenderMode() === 'ray-tracing' ||
          !!adapter.getAmbientOcclusion?.().enabled ||
          (keyName === 'view' && raster.pipeline !== 'deferred')
        if (keyName === 'pipeline')
          for (const option of field.input.options)
            option.disabled =
              (option.value === 'deferred' && adapter.getAntiAliasing().method === 'msaa') ||
              (option.value === 'forward' && adapter.getRenderMode() === 'hybrid')
        continue
      }
      if (key === 'aaMethod') {
        field.input.value = adapter.getAntiAliasing().method
        for (const option of field.input.options)
          option.disabled =
            (option.value === 'msaa' && adapter.getRasterSettings?.().pipeline === 'deferred') ||
            (option.value === 'taa' && adapter.getRenderMode() === 'hybrid')
        field.input.disabled =
          adapter.getRenderMode() === 'ray-tracing' || !!adapter.getAmbientOcclusion?.().enabled
        field.input.title = field.input.disabled
          ? 'Available for raster rendering without AO diagnostic'
          : ''
        continue
      } else if (key.startsWith('animation')) {
        const state = adapter.getAnimationState()
        if (key === 'animationClip') field.input.value = state.clip
        else if (key === 'animation_playing' || key === 'animation_loop')
          field.input.checked = state[key.slice(10)]
        else {
          const value = key === 'animationSpeed' ? state.speed : state.time
          field.input.value = value
          field.output.textContent = value.toFixed(2)
          if (key === 'animationTime')
            field.input.max = adapter.getAnimations()[state.clip].duration
        }
        continue
      }
      if (key === 'integrator') {
        field.input.value = adapter.getPathTracing().integrator
        continue
      }
      if (key === 'denoise' || key === 'lightSampling') {
        field.input.checked =
          key === 'lightSampling' && adapter.getPathTracing().integrator !== 'path'
            ? true
            : adapter.getPathTracing()[key]
        if (key === 'lightSampling')
          field.input.disabled = adapter.getPathTracing().integrator !== 'path'
        continue
      }
      if (key === 'renderMode') {
        field.input.value = adapter.getRenderMode()
        continue
      }
      if (key === 'samples') {
        field.output.textContent =
          (adapter.getPathTracing().integrator === 'mlt' ? 'Mutations/pixel: ' : 'Samples: ') +
          adapter.getAccumulatedSamples()
        continue
      }
      if (field.asset) {
        const current = assets[key]
        if (current && ![...field.input.options].some((option) => option.value === current)) {
          const option = document.createElement('option')
          option.value = current
          option.textContent = 'Custom'
          field.input.append(option)
        }
        field.input.value = current || ''
        field.input.disabled = loading
        field.input.setAttribute('aria-busy', String(loading))
        continue
      }
      if (key === 'showBackground') {
        field.input.checked = environment.showBackground
        continue
      }
      if (key === 'background') {
        field.input.disabled = current.environment && environment.showBackground
        field.input.title = field.input.disabled ? 'Used when skybox is hidden' : ''
        field.input.value = adapter.getClear()[0] > 0.5 ? 'light' : 'dark'
        continue
      }
      if (key.startsWith('mlt'))
        field.input.closest('label').style.display =
          adapter.getPathTracing().integrator === 'mlt' ? '' : 'none'
      if (key === 'photonsPerIteration' || key === 'photonRadius')
        field.input.closest('label').style.display =
          adapter.getPathTracing().integrator === 'photon-mapping' ? '' : 'none'
      field.input.value = values[key]
      field.output.textContent = Number(values[key]).toFixed(
        key === 'mltMutationScale'
          ? 4
          : key === 'mltLargeStepProbability'
            ? 2
            : key === 'photonRadius'
              ? 3
              : key === 'distance' || key === 'resolutionScale' || ['R', 'G', 'B'].includes(key)
                ? 2
                : [
                      'samplesPerFrame',
                      'maxBounces',
                      'photonsPerIteration',
                      'mltBootstrapSamples',
                    ].includes(key)
                  ? 0
                  : 1,
      )
    }
  }
  const resizeObserver = new ResizeObserver(() => {
    refresh()
  })
  resizeObserver.observe(canvas)
  setCollapsed(canvas.getBoundingClientRect().width < 520)
  refresh()
  return {
    refresh,
    dispose() {
      if (disposed) return
      disposed = true
      events.abort()
      fieldEvents.abort()
      resizeObserver.disconnect()
      if (frame) cancelAnimationFrame(frame)
      wrapper.before(canvas)
      wrapper.remove()
    },
  }
}
