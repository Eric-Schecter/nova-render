import * as Nova from '../src/Nova.js'
import { copyAmbientOcclusion } from '../src/AmbientOcclusion.js'
import { attachAOPanel } from './AOPanel.js'
import { createFrameMeter } from './FrameMeter.js'
export const aoCameras = {
  courtyard: { yaw: 90, pitch: 3, distance: 1.5, exposure: 1, target: [-1.35, -0.47850394, -0.25] },
  arcade: { yaw: 90, pitch: 3, distance: 1.5, exposure: 1, target: [-1.35, -0.47850394, 0.12] },
  overview: { yaw: 35, pitch: 20, distance: 2.8, exposure: 1, target: [0, 0, 0] },
}
export async function showAO(canvas, modelURL, shaderDirectory, camera, { onError } = {}) {
  const renderer = await Nova.createRenderer({ canvas, shaderDirectory }),
    meter = createFrameMeter(),
    events = new AbortController()
  let panel,
    orbit,
    observer,
    frame = 0,
    settle = 0,
    disposed = false,
    drawing = false,
    pending = true,
    dirty = true,
    running,
    adaptive = true,
    interactionUntil = 0,
    applied = '',
    dimensions = '',
    lastFullMs = 0
  let requested = copyAmbientOcclusion({ enabled: true, method: 'hbao', strength: 1.5 })
  const report = (error) => {
    if (!disposed) onError?.(error)
  }
  const visible = () => {
    const r = canvas.getBoundingClientRect()
    return (
      !document.hidden &&
      !canvas.closest('[aria-hidden="true"],[inert]') &&
      r.width > 0 &&
      r.height > 0 &&
      r.bottom > 0 &&
      r.top < innerHeight &&
      r.right > 0 &&
      r.left < innerWidth
    )
  }
  const preview = () => adaptive && performance.now() < interactionUntil && lastFullMs > 25
  function schedule() {
    if (disposed || frame || !visible()) return
    frame = requestAnimationFrame(() => {
      frame = 0
      render().catch(report)
    })
  }
  function request(interaction = true) {
    dirty = true
    pending = true
    if (interaction) interactionUntil = performance.now() + 220
    schedule()
  }
  async function render() {
    if (disposed) return 'skipped'
    if (drawing) {
      pending = true
      return running
    }
    if (!visible()) return 'skipped'
    drawing = true
    pending = false
    running = (async () => {
      const interacting = preview(),
        next = { ...requested }
      if (interacting) {
        next.resolutionScale = Math.min(
          requested.resolutionScale,
          Math.max(
            0.125,
            Math.min(
              0.25,
              240 / Math.max(1, canvas.clientWidth * (devicePixelRatio || 1)),
              180 / Math.max(1, canvas.clientHeight * (devicePixelRatio || 1)),
            ),
          ),
        )
        next.samples = Math.min(next.samples, 4)
      }
      const signature = JSON.stringify(next),
        size = `${canvas.clientWidth}:${canvas.clientHeight}:${devicePixelRatio}`
      if (signature !== applied) {
        renderer.setAmbientOcclusion(next)
        applied = signature
        dirty = true
      }
      if (size !== dimensions) {
        dimensions = size
        dirty = true
      }
      const accumulating = requested.method === 'rtao' && renderer.getAccumulatedSamples() < 32
      if (!dirty && !accumulating) return 'cached'
      dirty = false
      const start = performance.now(),
        result = await renderer.drawFrame()
      if (disposed) return result
      if (result !== 'skipped') {
        const ms = meter.record(start, {
          width: Math.max(1, Math.round(canvas.width * next.resolutionScale)),
          height: Math.max(1, Math.round(canvas.height * next.resolutionScale)),
          scale: next.resolutionScale,
          samples: next.samples,
          method: next.method,
          interactive: interacting,
        })
        if (!interacting) lastFullMs = ms
      }
      panel?.refresh()
      return result
    })()
    try {
      return await running
    } finally {
      drawing = false
      running = undefined
      if (!disposed) {
        if (pending || (requested.method === 'rtao' && renderer.getAccumulatedSamples() < 32))
          schedule()
        clearTimeout(settle)
        if (applied !== JSON.stringify(requested) || performance.now() < interactionUntil)
          settle = setTimeout(
            () => request(false),
            Math.max(0, interactionUntil - performance.now()) + 1,
          )
      }
    }
  }
  const controller = {
    getAmbientOcclusion: () => ({ ...requested }),
    setAmbientOcclusion(value) {
      requested = copyAmbientOcclusion(value)
      request()
    },
    getCamera: () => renderer.getCamera(),
    setCamera(value) {
      renderer.setCamera(value)
      request()
    },
    getAccumulatedSamples: () => renderer.getAccumulatedSamples(),
    getAdaptiveQuality: () => adaptive,
    setAdaptiveQuality(value) {
      if (typeof value !== 'boolean') throw Error('Adaptive quality requires a boolean')
      adaptive = value
      request(false)
    },
    getPerformanceStats() {
      const state = !visible()
        ? 'paused'
        : performance.now() < interactionUntil
          ? 'interactive'
          : drawing
            ? 'rendering'
            : requested.method === 'rtao' && renderer.getAccumulatedSamples() < 32
              ? 'accumulating'
              : 'idle'
      return {
        ...meter.snapshot(state),
        accumulatedFrames: renderer.getAccumulatedSamples(),
        adaptive,
      }
    },
    drawFrame: () => render(),
    dispose() {
      if (disposed) return
      disposed = true
      cancelAnimationFrame(frame)
      clearTimeout(settle)
      events.abort()
      observer?.disconnect()
      orbit?.dispose()
      panel?.dispose()
      renderer.dispose()
    },
  }
  try {
    renderer.setContinuousRendering(false)
    await renderer.loadModel(modelURL)
    renderer.setCamera(camera?.target ? camera : aoCameras.courtyard)
    orbit = Nova.attachOrbitControls(canvas, {
      getCamera: () => renderer.getCamera(),
      setCamera: (v) => {
        renderer.setCamera(v)
        request()
      },
      drawFrame: () => controller.drawFrame(),
      onError: report,
    })
    panel = attachAOPanel(canvas, controller, aoCameras, report)
    observer = new IntersectionObserver((entries) => {
      if (entries.some((e) => e.isIntersecting)) schedule()
    })
    observer.observe(canvas)
    document.addEventListener(
      'visibilitychange',
      () => {
        if (!document.hidden) schedule()
      },
      { signal: events.signal },
    )
    await controller.drawFrame()
    return controller
  } catch (error) {
    controller.dispose()
    throw error
  }
}
