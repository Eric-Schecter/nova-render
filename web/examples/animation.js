import * as Nova from '../src/Nova.js'
export async function showAnimation(
  canvas,
  modelURL,
  shaderDirectory,
  camera,
  { onChange, onError } = {},
) {
  const renderer = await Nova.createRenderer({ canvas, shaderDirectory })
  try {
    await renderer.loadModel(modelURL)
    renderer.setLighting('pbr')
    renderer.setCamera(camera)
    // Standard glTF skin + animation clips. The renderer owns sampling and skinning.
    renderer.setAnimationState({ clip: 0, time: 0, speed: 1, playing: true, loop: true })
    await renderer.drawFrame()
    renderer.enableOrbitControls({ onChange, onError })
    renderer.enableParameterPanel({ onError })
    return renderer
  } catch (error) {
    renderer.dispose()
    throw error
  }
}
