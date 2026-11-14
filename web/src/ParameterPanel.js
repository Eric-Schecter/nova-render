// Optional DOM panel; comparison controls occupy a separate row below the canvas.
const dark = [0.025, 0.035, 0.055, 1]
const light = [0.8, 0.85, 0.95, 1]
const styles = `
.nova-viewport{position:relative;width:100%;isolation:isolate}
.nova-render-surface{position:relative;width:100%;min-width:0}
.nova-panel{position:absolute;z-index:1;top:12px;right:12px;width:250px;max-width:calc(100% - 24px);max-height:calc(100% - 24px);overflow:auto;overscroll-behavior:contain;background:#181e29f5;border:1px solid #3c4659;border-radius:7px;color:#e1e6f0;box-shadow:0 4px 16px #0004;font:12px/1.4 system-ui,sans-serif;text-align:left;color-scheme:dark}
.nova-comparison .nova-panel{box-sizing:border-box;position:relative;top:auto;right:auto;width:100%;max-width:none;max-height:none;margin-top:12px;overflow:hidden;background:#181e29;box-shadow:none}
.nova-comparison .nova-panel-body{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(220px,100%),1fr));align-items:start;gap:12px;padding:12px;max-height:360px;overflow:auto;overscroll-behavior:contain}
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
  panel.setAttribute('aria-label', '参数面板')
  const toggle = document.createElement('button')
  toggle.type = 'button'
  toggle.className = 'nova-panel-toggle'
  const body = document.createElement('div')
  body.className = 'nova-panel-body'
  panel.append(toggle, body)
  wrapper.append(panel)
  const legend = document.createElement('div')
  legend.setAttribute('aria-label', '积分器对比结果')
  legend.style.cssText =
    'position:absolute;top:0;left:0;right:0;display:none;grid-template-columns:repeat(4,minmax(0,1fr));pointer-events:none;z-index:2;color:white;font:12px/1.5 monospace'
  surface.append(legend)
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
      label === '重置累计'
        ? 'Reset accumulation'
        : label === '重置相机'
          ? 'Reset camera'
          : label === '重置环境'
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
    if (current.camera) {
      const renderer = group('Renderer'),
        label = document.createElement('label'),
        select = document.createElement('select')
      label.textContent = 'Mode'
      select.setAttribute('aria-label', '渲染模式')
      const capabilities = adapter.getCapabilities()
      for (const [value, text] of [
        ['raster', 'Raster'],
        ['ray-tracing', 'Ray tracing'],
        ['integrator-comparison', 'Integrator comparison'],
      ]) {
        const option = document.createElement('option')
        option.value = value
        option.textContent = text
        if (value !== 'raster' && !capabilities.pathTracing) {
          option.disabled = true
          option.title = capabilities.reason
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
      if (current.pathTracing) {
        if (current.comparison) {
          const note = document.createElement('div')
          note.textContent = 'Emitter-only · no denoising · 1 spp / mpp per iteration'
          renderer.append(note)
          const label = document.createElement('label'),
            budget = document.createElement('select')
          label.textContent = 'Budget'
          budget.setAttribute('aria-label', '对比预算')
          for (const [value, text] of [
            ['samples', 'Fixed samples'],
            ['time', 'Fixed time'],
          ]) {
            const option = document.createElement('option')
            option.value = value
            option.textContent = text
            budget.append(option)
          }
          budget.value = adapter.getPathTracing().comparisonMilliseconds ? 'time' : 'samples'
          budget.addEventListener(
            'change',
            () =>
              change(() =>
                adapter.setPathTracing({
                  ...adapter.getPathTracing(),
                  comparisonMilliseconds: budget.value === 'time' ? 1000 : 0,
                }),
              ),
            { signal: fieldEvents.signal },
          )
          label.append(budget)
          renderer.append(label)
          fields.set('comparisonBudget', { input: budget })
          slider(
            renderer,
            'comparisonSamples',
            'Samples / method',
            '对比采样预算',
            1,
            65536,
            1,
            (value) =>
              adapter.setPathTracing({ ...adapter.getPathTracing(), comparisonSamples: value }),
          )
          slider(
            renderer,
            'comparisonMilliseconds',
            'Time / method (ms)',
            '对比时间预算',
            1,
            60000,
            1,
            (value) =>
              adapter.setPathTracing({
                ...adapter.getPathTracing(),
                comparisonMilliseconds: value,
              }),
          )
        }

        const integratorRow = document.createElement('label'),
          integrator = document.createElement('select')
        integratorRow.textContent = 'Integrator'
        integrator.setAttribute('aria-label', '路径积分器')
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
          ['samplesPerFrame', 'Samples per frame', '每帧采样', 1, 4, 1],
          ['maxBounces', 'Max bounces', '反弹次数', 1, 8, 1],
          ['resolutionScale', 'Render scale', '渲染比例', 0.25, 1, 0.05],
          ['mltBootstrapSamples', 'MLT bootstrap samples', 'MLT 预采样数', 256, 1048576, 256],
          ['mltLargeStepProbability', 'MLT large-step probability', 'MLT 大步概率', 0.05, 1, 0.01],
          ['mltMutationScale', 'MLT mutation scale', 'MLT 小步幅度', 0.0001, 0.25, 0.0001],
          ['photonsPerIteration', 'Photons per iteration', '每轮光子数', 256, 65536, 256],
          ['photonRadius', 'Initial gather radius', '光子聚集半径', 0.03, 0.5, 0.005],
        ])
          slider(renderer, key, caption, aria, min, max, step, (value) =>
            adapter.setPathTracing({ ...adapter.getPathTracing(), [key]: value }),
          )
        const denoiseRow = document.createElement('label'),
          denoise = document.createElement('input')
        denoiseRow.className = 'nova-panel-check'
        denoise.type = 'checkbox'
        denoise.setAttribute('aria-label', '降噪')
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
        sampling.setAttribute('aria-label', '光源采样与 MIS')
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
        samples.setAttribute('aria-label', '累计采样')
        renderer.append(samples)
        fields.set('samples', { output: samples })
        const method = document.createElement('div')
        method.textContent = 'WebGPU compute BVH'
        renderer.append(method)
        reset(renderer, '重置累计', () => adapter.resetAccumulation())
      }
    }
    const clips = adapter.getAnimations()
    if (clips.length) {
      const animation = group('Animation'),
        label = document.createElement('label'),
        select = document.createElement('select')
      label.textContent = 'Clip'
      select.setAttribute('aria-label', '动画片段')
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
        ['playing', 'Playing', '播放动画'],
        ['loop', 'Loop', '循环动画'],
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
      slider(animation, 'animationSpeed', 'Speed', '动画速度', 0.1, 4, 0.1, (value) =>
        adapter.setAnimationState({ ...adapter.getAnimationState(), speed: value }),
      )
      slider(
        animation,
        'animationTime',
        'Time',
        '动画时间',
        0,
        clips[adapter.getAnimationState().clip].duration,
        0.01,
        (value) => adapter.setAnimationState({ ...adapter.getAnimationState(), time: value }),
      )
    }
    if (models.length) {
      const assets = group('Model')
      assetSelect(assets, 'model', 'Model', '模型选择', models, adapter.loadModel)
    }
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
      { signal: fieldEvents.signal },
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
      if (current.lighting || current.environment)
        slider(camera, 'exposure', 'Exposure', '曝光', 0.1, 16, 0.1, (value) =>
          adapter.setCamera({ ...adapter.getCamera(), exposure: value }),
        )
      reset(camera, '重置相机', () => adapter.setCamera(initialCamera))
    }
    if (current.environment && !current.comparison) {
      const environment = group('Environment'),
        checkboxRow = document.createElement('label'),
        checkbox = document.createElement('input')
      assetSelect(
        environment,
        'environment',
        'Map',
        '环境贴图',
        environments,
        adapter.loadEnvironment,
      )
      checkboxRow.className = 'nova-panel-check'
      checkbox.type = 'checkbox'
      checkbox.setAttribute('aria-label', '显示天空盒')
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
        ['rotation', 'Rotation', '环境旋转', -180, 180, 0.1],
        ['environmentIntensity', 'Intensity', '环境强度', 0, 16, 0.01],
        ['backgroundIntensity', 'Sky brightness', '天空盒强度', 0, 16, 0.01],
      ])
        slider(environment, key, caption, aria, min, max, step, (value) =>
          adapter.setEnvironment({
            ...adapter.getEnvironment(),
            [key === 'environmentIntensity' ? 'intensity' : key]: value,
          }),
        )
      reset(environment, '重置环境', () => adapter.setEnvironment(initialEnvironment))
    }
    if (current.lighting && !current.comparison) {
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
    const comparing = !!current.comparison
    wrapper.classList.toggle('nova-comparison', comparing)
    legend.style.display = comparing ? 'grid' : 'none'
    const side = Math.min(canvas.clientWidth, canvas.clientHeight)
    legend.style.width = `${side}px`
    legend.style.height = `${side}px`
    legend.style.left = `${(canvas.clientWidth - side) / 2}px`
    legend.style.top = `${(canvas.clientHeight - side) / 2}px`
    legend.style.gridTemplateColumns = 'repeat(2,minmax(0,1fr))'
    legend.style.gridTemplateRows = 'repeat(2,minmax(0,1fr))'
    if (comparing) {
      legend.replaceChildren(
        ...(adapter.getComparisonProgress?.() || []).map((p, i) => {
          const cell = document.createElement('span')
          cell.style.cssText = 'padding:4px;overflow:hidden;align-self:start;background:#111c'
          cell.textContent =
            ['PT', 'BDPT', 'Photon mapping', 'MLT'][i] +
            ' · ' +
            p.samples +
            (i === 3 ? ' mpp · ' : ' spp · ') +
            Math.round(p.milliseconds) +
            ' ms' +
            (p.complete ? ' · paused' : '')
          return cell
        }),
      )
    }
    const assets = adapter.getAssets()
    for (const [key, field] of fields) {
      if (key === 'comparisonBudget') {
        field.input.value = values.comparisonMilliseconds ? 'time' : 'samples'
        continue
      }
      if (['comparisonSamples', 'comparisonMilliseconds'].includes(key))
        field.input.closest('label').style.display =
          (key === 'comparisonMilliseconds') === !!values.comparisonMilliseconds ? '' : 'none'
      if (['integrator', 'samplesPerFrame', 'denoise', 'lightSampling'].includes(key))
        field.input.closest('label').style.display = comparing ? 'none' : ''

      if (key.startsWith('animation')) {
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
        field.input.disabled = comparing || (current.environment && environment.showBackground)
        field.input.title = field.input.disabled ? 'Used when skybox is hidden' : ''
        field.input.value = adapter.getClear()[0] > 0.5 ? 'light' : 'dark'
        continue
      }
      if (key.startsWith('mlt'))
        field.input.closest('label').style.display =
          comparing || adapter.getPathTracing().integrator === 'mlt' ? '' : 'none'
      if (key === 'photonsPerIteration' || key === 'photonRadius')
        field.input.closest('label').style.display =
          comparing || adapter.getPathTracing().integrator === 'photon-mapping' ? '' : 'none'
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
  setCollapsed(!adapter.getMode().comparison && canvas.getBoundingClientRect().width < 520)
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
