import * as Nova from '../src/Nova.js'
import { modelChoices } from './AssetChoices.js'
export async function showComparison(canvas, modelURL, shaderDirectory, camera, { onError } = {}) {
  const renderer = await Nova.createRenderer({ canvas, shaderDirectory })
  try {
    await renderer.loadModel(modelURL)
    renderer.setCamera(camera)
    renderer.setRayTracing({
      ...renderer.getRayTracing(),
      maxBounces: 8,
      photonRadius: 0.1,
      comparisonSamples: 64,
      comparisonMilliseconds: 0,
    })
    renderer.setRenderMode('integrator-comparison')
    await renderer.drawFrame()
    renderer.enableOrbitControls({ onError })
    renderer.enableParameterPanel({ onError, models: modelChoices(modelURL) })
    return renderer
  } catch (error) {
    renderer.dispose()
    throw error
  }
}
