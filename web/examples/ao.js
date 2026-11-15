import * as Nova from '../src/Nova.js'
import { attachAOPanel } from './AOPanel.js'
export const aoCameras = {
  courtyard: { yaw: 90, pitch: 3, distance: 1.5, exposure: 1, target: [-1.35, -0.47850394, -0.25] },
  arcade: { yaw: 90, pitch: 3, distance: 1.5, exposure: 1, target: [-1.35, -0.47850394, 0.12] },
  overview: { yaw: 35, pitch: 20, distance: 2.8, exposure: 1, target: [0, 0, 0] },
}
export async function showAO(canvas, modelURL, shaderDirectory, camera, { onError } = {}) {
  const renderer = await Nova.createRenderer({ canvas, shaderDirectory })
  let panel,
    orbit,
    frame = 0,
    disposed = false,
    drawing = false,
    pending = false
  const report = (error) => {
    if (!disposed) onError?.(error)
  }
  const controller = {
    getAmbientOcclusion: () => renderer.getAmbientOcclusion(),
    setAmbientOcclusion(value) {
      renderer.setAmbientOcclusion(value)
      request()
    },
    getCamera: () => renderer.getCamera(),
    setCamera(value) {
      renderer.setCamera(value)
      request()
    },
    getAccumulatedSamples: () => renderer.getAccumulatedSamples(),
    async drawFrame() {
      if (disposed) return
      pending = true
      if (drawing) return
      drawing = true
      try {
        do {
          pending = false
          await renderer.drawFrame()
          panel?.refresh()
        } while (pending && !disposed)
      } finally {
        drawing = false
      }
      const ao = renderer.getAmbientOcclusion()
      if (ao.method === 'rtao' && renderer.getAccumulatedSamples() < 32 && !frame && !disposed) {
        frame = requestAnimationFrame(() => {
          frame = 0
          controller.drawFrame().catch(report)
        })
      }
    },
    dispose() {
      if (disposed) return
      disposed = true
      cancelAnimationFrame(frame)
      orbit?.dispose()
      panel?.dispose()
      renderer.dispose()
    },
  }
  function request() {
    controller.drawFrame().catch(report)
  }
  try {
    renderer.setContinuousRendering(false)
    await renderer.loadModel(modelURL)
    renderer.setCamera(camera?.target ? camera : aoCameras.courtyard)
    renderer.setAmbientOcclusion({ enabled: true, method: 'hbao', strength: 1.5 })
    orbit = Nova.attachOrbitControls(canvas, {
      getCamera: () => renderer.getCamera(),
      setCamera: (v) => renderer.setCamera(v),
      drawFrame: () => controller.drawFrame(),
      onError: report,
    })
    panel = attachAOPanel(canvas, controller, aoCameras, report)
    await controller.drawFrame()
    return controller
  } catch (error) {
    controller.dispose()
    throw error
  }
}
