import { createRenderer } from '../src/Nova.js'
import { instrumentRenderer } from './PerformancePanel.js'
import { createHybridScenes } from './HybridScenes.js'
export async function showHybrid(
  canvas,
  modelURL,
  shaderDirectory,
  camera,
  { onChange, onError, onHybridControls } = {},
) {
  const renderer = instrumentRenderer(await createRenderer({ canvas, shaderDirectory }), canvas)
  let scenes
  try {
    renderer.setPostProcessing({
      enabled: true,
      bloom: false,
      autoExposure: false,
      toneMapping: 'aces',
    })
    renderer.setHybrid({
      shadows: true,
      shadowSamples: 1,
      ambientOcclusion: true,
      reflections: true,
      reflectionSamples: 1,
      temporalReconstruction: true,
    })
    scenes = await createHybridScenes(renderer, canvas, shaderDirectory, { onChange, onError })
    await scenes.selectScene('reflections-test')
    renderer.enableParameterPanel({ onError })
    onHybridControls?.(scenes)
    const dispose = renderer.dispose.bind(renderer)
    renderer.dispose = () => {
      scenes.dispose()
      dispose()
    }
    return renderer
  } catch (error) {
    scenes?.dispose()
    renderer.dispose()
    throw error
  }
}
