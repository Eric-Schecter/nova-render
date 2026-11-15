import { instrumentRenderer } from './PerformancePanel.js'
import * as Nova from '../src/Nova.js'
import { modelChoices, environmentChoices } from './AssetChoices.js'

export async function showEnvironment(
  canvas,
  modelURL,
  shaderDirectory,
  camera,
  { environmentURL, onChange, onError } = {},
) {
  const renderer = instrumentRenderer(
    await Nova.createRenderer({ canvas, shaderDirectory }),
    canvas,
  )
  try {
    await renderer.loadModel(modelURL)
    renderer.setLighting('pbr')
    // Environment-only illumination; the panel can add a direct light later.
    renderer.setDirectionalLight({
      direction: [-1, 1.6, 2],
      color: [1, 3.2 / 3.5, 0.8],
      intensity: 0,
    })
    await renderer.loadEnvironment(environmentURL)
    renderer.setEnvironment({
      rotation: 0,
      intensity: 1,
      backgroundIntensity: 1,
      showBackground: true,
    })
    renderer.setCamera(camera)
    await renderer.drawFrame()
    renderer.enableOrbitControls({ onChange, onError })
    renderer.enableParameterPanel({
      onError,
      models: modelChoices(modelURL),
      environments: environmentChoices(environmentURL),
    })
    return renderer
  } catch (error) {
    renderer.dispose()
    throw error
  }
}
