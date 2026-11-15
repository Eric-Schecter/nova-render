import { instrumentRenderer } from './PerformancePanel.js'
import * as Nova from '../src/Nova.js'
import { modelChoices } from './AssetChoices.js'

export async function showLighting(
  canvas,
  modelURL,
  shaderDirectory,
  camera,
  {
    onChange,
    onError,
    light = { direction: [-1, 1.6, 2], color: [1, 3.2 / 3.5, 0.8], intensity: 3.5 },
  } = {},
) {
  const renderer = instrumentRenderer(
    await Nova.createRenderer({ canvas, shaderDirectory }),
    canvas,
  )
  try {
    await renderer.loadModel(modelURL)
    renderer.setLighting('pbr')
    renderer.setDirectionalLight(light)
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

// 相机交互：renderer.setCamera({ yaw, pitch, distance, exposure })
// 然后 await renderer.drawFrame()；页面退出时 renderer.dispose()。
