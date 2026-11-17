import { createRenderer } from '../src/Nova.js'
import { instrumentRenderer } from './PerformancePanel.js'
export async function showHybrid(
  canvas,
  modelURL,
  shaderDirectory,
  camera,
  { onChange, onError } = {},
) {
  const renderer = instrumentRenderer(await createRenderer({ canvas, shaderDirectory }), canvas)
  try {
    const world = (x, y, z, tx = 0, ty = 0, tz = 0) => [
      x,
      0,
      0,
      0,
      0,
      y,
      0,
      0,
      0,
      0,
      z,
      0,
      tx,
      ty,
      tz,
      1,
    ]
    await renderer.setScene(
      [{ id: 'box', url: shaderDirectory + 'SceneRed.glb' }],
      [
        { id: 'ground', assetId: 'box', world: world(2, 0.06, 2, 0, -0.8) },
        { id: 'caster', assetId: 'box', world: world(0.3, 1, 0.3) },
      ],
    )
    const material = renderer.getMaterials('box')[0]
    material.baseColor = [0.8, 0.8, 0.8, 1]
    material.metallic = 0
    material.roughness = 0.7
    await renderer.setMaterial(0, material, 'box')
    renderer.setLighting('pbr')
    renderer.setDirectionalLight({ direction: [-1, 2, 1], color: [1, 1, 1], intensity: 4 })
    renderer.setCamera({ yaw: 30, pitch: 25, distance: 5.5 })
    renderer.setHybrid({ shadows: true, shadowSamples: 4, ambientOcclusion: true })
    renderer.setRenderMode('hybrid')
    await renderer.drawFrame()
    renderer.enableOrbitControls({ onChange, onError })
    renderer.enableParameterPanel({ onError })
    return renderer
  } catch (error) {
    renderer.dispose()
    throw error
  }
}
