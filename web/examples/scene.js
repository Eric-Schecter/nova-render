import * as Nova from '../src/Nova.js'
import { instrumentRenderer } from './PerformancePanel.js'

export async function showScene(canvas, shaderDirectory = '/generated/') {
  const renderer = instrumentRenderer(
    await Nova.createRenderer({ canvas, shaderDirectory }),
    canvas,
  )
  try {
    // sourceSpace preserves the GLB scene's dimensions, origin and node transforms.
    const assets = [
      { id: 'red', url: shaderDirectory + 'SceneRed.glb', sourceSpace: true },
      { id: 'blue', url: shaderDirectory + 'SceneBlue.glb', sourceSpace: true },
    ]
    const instances = [
      { id: 'red-upper', assetId: 'red', x: -1.2, y: 0.8 },
      { id: 'red-lower', assetId: 'red', x: -1.2, y: -0.8 },
      { id: 'blue-right', assetId: 'blue', x: 1.2, y: 0 },
    ].map(({ id, assetId, x, y }) => ({
      id,
      assetId,
      world: [0.275, 0, 0, 0, 0, 0.275, 0, 0, 0, 0, 0.275, 0, x - 0.55, y - 0.275, 0, 1],
    }))
    await renderer.setScene(assets, instances)
    renderer.setCamera({ ...renderer.getCamera(), distance: 7 })
    await renderer.drawFrame()
    renderer.enableOrbitControls()
    return renderer
  } catch (error) {
    renderer.dispose()
    throw error
  }
}
