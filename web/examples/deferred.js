import * as Nova from '../src/Nova.js'
import { instrumentRenderer } from './PerformancePanel.js'
import { modelChoices } from './AssetChoices.js'
export async function showDeferred(
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
    renderer.setLighting('pbr')
    renderer.setCamera(camera)
    renderer.setRasterSettings({ pipeline: 'deferred' })
    await renderer.drawFrame()
    renderer.enableOrbitControls({ onChange, onError })
    renderer.enableParameterPanel({ onError, models: modelChoices(modelURL) })
    return renderer
  } catch (error) {
    renderer.dispose()
    throw error
  }
}
