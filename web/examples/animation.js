import { instrumentRenderer } from './PerformancePanel.js'
import * as Nova from '../src/Nova.js'
import { environmentChoices } from './AssetChoices.js'
export async function showAnimation(
  canvas,
  modelURL,
  shaderDirectory,
  camera,
  {
    onChange,
    onError,
    environmentURL = modelURL.slice(0, modelURL.lastIndexOf('/') + 1) + 'venice_sunset.bin',
  } = {},
) {
  const renderer = instrumentRenderer(
    await Nova.createRenderer({ canvas, shaderDirectory }),
    canvas,
  )
  try {
    await renderer.loadModel(modelURL)
    renderer.setLighting('pbr')
    await renderer.loadEnvironment(environmentURL)
    renderer.setEnvironment({
      rotation: 0,
      intensity: 1,
      backgroundIntensity: 1,
      showBackground: true,
    })
    renderer.setDirectionalLight({
      direction: [-1, 1.6, 2],
      color: [1, 3.2 / 3.5, 0.8],
      intensity: 3,
    })
    renderer.setShadows({ enabled: true })
    renderer.setCamera(camera)
    // Standard glTF skin + animation clips. The renderer owns sampling and skinning.
    renderer.setAnimationState({ clip: 0, time: 0, speed: 1, playing: true, loop: true })
    await renderer.drawFrame()
    renderer.enableOrbitControls({ onChange, onError })
    renderer.enableParameterPanel({ onError, environments: environmentChoices(environmentURL) })
    return renderer
  } catch (error) {
    renderer.dispose()
    throw error
  }
}
