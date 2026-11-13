import * as Nova from '../src/Nova.js'
import { modelChoices } from './AssetChoices.js'

export async function showHelmet(
  canvas,
  modelURL,
  shaderDirectory,
  camera,
  { onChange, onError } = {},
) {
  const renderer = await Nova.createRenderer({ canvas, shaderDirectory })
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

// 相机交互：renderer.setCamera({ yaw, pitch, distance, exposure })
// 然后 await renderer.drawFrame()；页面退出时 renderer.dispose()。
