import { createRenderer } from '../src/Nova.js'
import { instrumentRenderer } from './PerformancePanel.js'
export async function showMaterialAlpha(canvas, shaderDirectory) {
  const renderer = instrumentRenderer(await createRenderer({ canvas, shaderDirectory }), canvas)
  try {
    renderer.setAntiAliasing({ method: 'fxaa' })
    renderer.setLighting('unlit')
    renderer.setViewCamera({ position: [0, 0, 3.6] })
    renderer.setPostProcessing({ enabled: true, bloom: false, autoExposure: false })
    await renderer.loadModel(shaderDirectory + 'MaterialLayers.glb')
    await renderer.drawFrame()
    renderer.enableOrbitControls()
    return {
      renderer,
      async load(name) {
        await renderer.loadModel(shaderDirectory + 'Material' + name + '.glb')
        await renderer.drawFrame()
      },
      async update(slot, values) {
        await renderer.setMaterial(slot, { ...renderer.getMaterials()[slot], ...values })
        await renderer.drawFrame()
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
