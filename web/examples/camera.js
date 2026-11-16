import * as Nova from '../src/Nova.js'
import { instrumentRenderer } from './PerformancePanel.js'

export async function showCamera(canvas, shaderDirectory) {
  const renderer = instrumentRenderer(
    await Nova.createRenderer({ canvas, shaderDirectory }),
    canvas,
  )
  try {
    await renderer.setScene(
      [
        { id: 'red', url: shaderDirectory + 'SceneRed.glb' },
        { id: 'blue', url: shaderDirectory + 'SceneBlue.glb' },
      ],
      [
        {
          id: 'left',
          assetId: 'red',
          world: [0.7, 0, 0, 0, 0, 0.7, 0, 0, 0, 0, 0.7, 0, -1.1, 0, 0, 1],
        },
        {
          id: 'right',
          assetId: 'blue',
          world: [0.7, 0, 0, 0, 0, 0.7, 0, 0, 0, 0, 0.7, 0, 1.1, 0, 0, 1],
        },
      ],
    )
    renderer.setViewCamera({
      position: [0, 0, 6],
      orientation: [0, 0, 0, 1],
      verticalFov: 45,
      nearPlane: 0.1,
      farPlane: 100,
    })
    await renderer.drawFrame()
    return renderer
  } catch (error) {
    renderer.dispose()
    throw error
  }
}
