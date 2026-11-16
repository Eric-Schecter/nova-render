import * as Nova from '../src/Nova.js'
import { instrumentRenderer } from './PerformancePanel.js'
export async function showMaterials(canvas, shaderDirectory) {
  const renderer = instrumentRenderer(
    await Nova.createRenderer({ canvas, shaderDirectory }),
    canvas,
  )
  try {
    await renderer.loadModel(shaderDirectory + 'MaterialDual.glb')
    renderer.setViewCamera({
      position: [0, 0, 4],
      orientation: [0, 0, 0, 1],
      projection: 'orthographic',
      orthographicHeight: 2.5,
    })
    renderer.setLighting('pbr')
    renderer.setAntiAliasing({ method: 'fxaa' })
    await renderer.drawFrame()
    return {
      renderer,
      async update(slot, values) {
        const material = renderer.getMaterials()[slot]
        Object.assign(material, values)
        await renderer.setMaterial(slot, material)
        await renderer.drawFrame()
      },
      checker: {
        width: 2,
        height: 2,
        pixels: new Uint8Array([
          255, 255, 255, 255, 30, 30, 30, 255, 30, 30, 30, 255, 255, 255, 255, 255,
        ]),
      },
      dispose() {
        renderer.dispose()
      },
    }
  } catch (error) {
    renderer.dispose()
    throw error
  }
}
