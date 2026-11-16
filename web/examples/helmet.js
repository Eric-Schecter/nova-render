import { instrumentRenderer } from './PerformancePanel.js'
import * as Nova from '../src/Nova.js'
import { modelChoices } from './AssetChoices.js'

export async function showHelmet(
  canvas,
  modelURL,
  shaderDirectory,
  camera,
  { onChange, onError } = {},
) {
  const renderer = instrumentRenderer(
    await Nova.createRenderer({ canvas, shaderDirectory }),
    canvas,
  )
  try {
    await renderer.loadModel(modelURL)
    renderer.setCamera(camera)
    await renderer.drawFrame()
    renderer.enableOrbitControls({ onChange, onError })
    renderer.enableParameterPanel({ onError, models: modelChoices(modelURL) })
    return renderer
  } catch (error) {
    renderer.dispose()
    throw error
  }
}

// Camera interaction: renderer.setCamera({ yaw, pitch, distance, exposure })
// Then await renderer.drawFrame(); call renderer.dispose() on teardown.
