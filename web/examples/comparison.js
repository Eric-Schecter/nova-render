import * as Nova from '../src/Nova.js'
import { modelChoices } from './AssetChoices.js'
import { attachComparisonPanel } from './ComparisonPanel.js'
const methods = ['path', 'bidirectional', 'photon-mapping', 'mlt']
const names = ['PT', 'BDPT', 'Photon mapping', 'MLT']

// Example controller: Nova renders one view; this example owns all comparison policy.
export async function showComparison(host, modelURL, shaderDirectory, camera, { onError } = {}) {
  const style = document.createElement('style')
  style.textContent = `.comparison-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.comparison-tile{min-width:0;background:#080b10;border-radius:6px;overflow:hidden}.comparison-tile canvas{display:block;width:100%;height:auto;aspect-ratio:1}.comparison-label{padding:5px 7px;color:#e1e6f0;background:#151b26;font:12px/1.5 monospace;min-height:28px;overflow-wrap:anywhere}.comparison-controls{margin-top:12px;border:1px solid #3c4659;border-radius:7px;background:#181e29;color:#e1e6f0;font:12px/1.4 system-ui;color-scheme:dark}.comparison-controls button,.comparison-controls select,.comparison-controls input{font:inherit;color:inherit}.comparison-controls button,.comparison-controls select{background:#25324b;border:1px solid #465776;border-radius:4px;padding:5px}.comparison-controls-toggle{width:100%;text-align:left}.comparison-controls-body{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(220px,100%),1fr));gap:14px;padding:12px;max-height:360px;overflow:auto}.comparison-controls-body[hidden]{display:none}.comparison-controls fieldset{border:0;min-width:0;margin:0;padding:0}.comparison-controls legend{font-weight:600;color:#b9ccff}.comparison-controls label{display:flex;flex-direction:column;gap:3px;margin:8px 0}.comparison-controls select,.comparison-controls input{width:100%;box-sizing:border-box}.comparison-controls input[type=range]{accent-color:#819eff}`
  const grid = document.createElement('div')
  grid.className = 'comparison-grid'
  host.append(style, grid)
  let views = [],
    labels = [],
    disposed = false,
    frame = 0,
    tail = Promise.resolve(),
    failure = false,
    previousSize = '',
    loading = false
  let budget = { samples: 64, milliseconds: 0 },
    settings,
    cameraValue = { ...camera },
    currentModel = modelURL
  let progress = methods.map((integrator) => ({
    integrator,
    samples: 0,
    milliseconds: 0,
    complete: false,
  }))
  let orbit
  let panel
  const report = (error) => {
    failure = true
    host.dispatchEvent(new CustomEvent('nova-error', { detail: error }))
    onError?.(error)
  }
  const enqueue = (action) => {
    const next = tail.then(() => {
      if (disposed) throw Error('Comparison scene disposed')
      return action()
    })
    tail = next.catch(() => {})
    return next
  }
  function updateLabels() {
    labels.forEach((label, i) => {
      const p = progress[i]
      label.textContent = `${names[i]} · ${p.samples} ${i === 3 ? 'mpp' : 'spp'} · ${Math.round(p.milliseconds)} ms${p.complete ? ' · paused' : ''}`
    })
  }
  function schedule() {
    if (disposed || failure || frame || progress.every((p) => p.complete)) return
    frame = requestAnimationFrame(() => {
      frame = 0
      controller.drawFrame().catch(report)
    })
  }
  function reset() {
    views.forEach((v) => v.resetAccumulation())
    progress = methods.map((integrator) => ({
      integrator,
      samples: 0,
      milliseconds: 0,
      complete: false,
    }))
    failure = false
    updateLabels()
    panel?.refresh()
    schedule()
  }
  async function load(url) {
    loading = true
    panel?.refresh()
    const next = [],
      tiles = [],
      nextLabels = []
    try {
      for (let i = 0; i < 4; i++) {
        const tile = document.createElement('section'),
          label = document.createElement('div'),
          canvas = document.createElement('canvas')
        tile.className = 'comparison-tile'
        label.className = 'comparison-label'
        label.setAttribute('aria-label', names[i] + ' 统计')
        canvas.setAttribute('aria-label', names[i] + ' 渲染结果')
        tile.append(label, canvas)
        tiles.push(tile)
        nextLabels.push(label)
        // Attach with zero layout impact until the complete replacement is ready.
        const view = await Nova.createRenderer({ canvas, shaderDirectory })
        next.push(view)
        view.setContinuousRendering(false)
        await view.loadModel(url)
        view.setLighting('pbr')
        view.setClearColor([0, 0, 0, 1])
        view.setEnvironment({
          rotation: 0,
          intensity: 0,
          backgroundIntensity: 0,
          showBackground: false,
        })
        view.setDirectionalLight({ direction: [0, 1, 0], color: [1, 1, 1], intensity: 0 })
        view.setCamera(cameraValue)
        settings ??= { ...view.getRayTracing(), maxBounces: 8, photonRadius: 0.1 }
        view.setRayTracing({
          ...settings,
          integrator: methods[i],
          samplesPerFrame: 1,
          denoise: false,
          lightSampling: true,
        })
        view.setRenderMode('ray-tracing')
      }
      if (disposed) throw Error('Comparison scene disposed')
      views.forEach((v) => v.dispose())
      views = next
      labels = nextLabels
      grid.replaceChildren(...tiles)
      currentModel = url
      previousSize = ''
      reset()
    } catch (error) {
      next.forEach((v) => v.dispose())
      throw error
    } finally {
      loading = false
      panel?.refresh()
    }
  }
  const controller = {
    getProgress: () => progress.map((p) => ({ ...p })),
    getModel: () => currentModel,
    isLoading: () => loading,
    getCamera: () => ({ ...cameraValue }),
    setCamera(value) {
      const next = { ...cameraValue, ...value }
      views.forEach((v) => v.setCamera(next))
      cameraValue = next
      reset()
    },
    getRayTracing: () => ({ ...settings }),
    setRayTracing(value) {
      views.forEach((v, i) =>
        v.setRayTracing({
          ...value,
          integrator: methods[i],
          samplesPerFrame: 1,
          denoise: false,
          lightSampling: true,
        }),
      )
      settings = { ...value }
      reset()
    },
    getBudget: () => ({ ...budget }),
    setBudget(value) {
      if (
        !Number.isInteger(value.samples) ||
        value.samples < 1 ||
        value.samples > 65536 ||
        !Number.isInteger(value.milliseconds) ||
        value.milliseconds < 0 ||
        value.milliseconds > 60000
      )
        throw Error('Invalid comparison budget')
      budget = { ...value }
      reset()
    },
    loadModel: (url) => enqueue(() => load(url)),
    resetAccumulation: reset,
    drawFrame: () =>
      enqueue(async () => {
        const bounds = grid.getBoundingClientRect()
        if (
          document.hidden ||
          host.closest('[aria-hidden="true"],[inert]') ||
          !bounds.width ||
          bounds.bottom < 0 ||
          bounds.top > innerHeight
        ) {
          schedule()
          return 'skipped'
        }
        const size = `${bounds.width}:${window.devicePixelRatio}`
        if (size !== previousSize) {
          previousSize = size
          reset()
        }
        for (let i = 0; i < 4; i++)
          if (!progress[i].complete) {
            const start = performance.now()
            await views[i].drawFrame()
            const p = progress[i]
            p.milliseconds += performance.now() - start
            p.samples = views[i].getAccumulatedSamples()
            p.complete =
              budget.milliseconds > 0
                ? p.milliseconds >= budget.milliseconds
                : p.samples >= budget.samples
          }
        updateLabels()
        panel?.refresh()
        schedule()
        return 'rendered'
      }),
    dispose() {
      if (disposed) return
      disposed = true
      if (frame) cancelAnimationFrame(frame)
      orbit?.dispose()
      panel?.dispose()
      views.forEach((v) => v.dispose())
      grid.remove()
      style.remove()
    },
  }
  try {
    await load(modelURL)
    orbit = Nova.attachOrbitControls(grid, {
      getCamera: () => controller.getCamera(),
      setCamera: (value) => controller.setCamera(value),
      drawFrame: () => controller.drawFrame(),
      onError: report,
    })
    panel = attachComparisonPanel(host, controller, modelChoices(modelURL), report)
    await controller.drawFrame()
    return controller
  } catch (error) {
    controller.dispose()
    throw error
  }
}
