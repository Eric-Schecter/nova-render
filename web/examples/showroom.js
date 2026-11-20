import { createRenderer } from '../src/Nova.js'
import { instrumentRenderer } from './PerformancePanel.js'
import { attachShowroomProbes } from './ShowroomProbes.js'

export async function showShowroom(canvas, _modelURL, shaderDirectory, _camera, { onError } = {}) {
  const renderer = instrumentRenderer(await createRenderer({ canvas, shaderDirectory }), canvas)
  const assets = shaderDirectory + 'showroom/'
  try {
    renderer.setClearColor([0.2, 0.2, 0.2, 1])
    renderer.setLighting('pbr')
    await renderer.setScene(
      [
        { id: 'car', url: assets + 'CarConcept.glb', sourceSpace: true },
        { id: 'floor', url: assets + 'Ground.glb', sourceSpace: true },
      ],
      [
        {
          id: 'car',
          assetId: 'car',
          world: [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.005216949, -0.00122019, -0.23846179, 1],
        },
        { id: 'floor', assetId: 'floor' },
      ],
    )
    const [floorMaterial] = renderer.getMaterials('floor')
    await renderer.setMaterial(
      0,
      { ...floorMaterial, roughness: 0.35, hybridReflectionStrength: 1 },
      'floor',
    )
    await renderer.loadEnvironment(assets + 'CleanStudio.bin')
    renderer.setEnvironment({
      // Match the equirectangular HDRI orientation of the UE reference.
      rotation: -90,
      intensity: 1,
      backgroundIntensity: 1,
      showBackground: false,
    })
    renderer.setPostProcessing({
      enabled: true,
      bloom: false,
      autoExposure: false,
      toneMapping: 'aces',
    })
    renderer.setViewCamera({
      position: [4.0087951, 1.6122328, 4.052369],
      orientation: [-0.078989, 0.37879612, 0.03246848, 0.92153135],
      verticalFov: 40,
      nearPlane: 0.05,
      farPlane: 150,
      exposure: 0.85,
    })
    renderer.setLights([
      {
        id: 'hood',
        type: 'rectangle',
        position: [-0.4, 3.25, 1.15],
        direction: [0, -1, 0],
        up: [0, 0, 1],
        width: 4.7,
        height: 1.4,
        intensity: 2.5,
        color: [1, 0.94, 0.86],
        castShadow: true,
      },
      {
        id: 'side',
        type: 'rectangle',
        position: [3.15, 1.25, -0.35],
        direction: [-1, -0.15, 0],
        up: [0, 1, 0],
        width: 5.1,
        height: 0.55,
        intensity: 3.2,
        color: [0.88, 0.95, 1],
        castShadow: false,
      },
      {
        id: 'fill',
        type: 'rectangle',
        position: [2.9, 0.85, -0.65],
        direction: [-1, 0.14, -0.08],
        up: [0, 1, 0],
        width: 5.8,
        height: 1.35,
        intensity: 0.55,
        color: [1, 1, 1],
        castShadow: false,
      },
    ])
    renderer.setHybrid({
      shadows: true,
      shadowSamples: 4,
      probeGI: true,
      probeBounds: { minimum: [-2.65, -0.12, -3.25], maximum: [2.65, 1.95, 3.25] },
      reflections: true,
      reflectionStrength: 0.25,
      reflectionSamples: 1,
      halfResolutionReflections: true,
      reflectionDenoising: false,
    })
    renderer.setRenderMode('hybrid')
    renderer.setAntiAliasing({ method: 'fxaa' })
    await renderer.drawFrame()
    let settleTimer
    renderer.enableOrbitControls({
      viewDistance: 5.784566,
      onError,
      onChange() {
        if (renderer.getHybrid().shadowSamples !== 2) renderer.setHybrid({ shadowSamples: 2 })
        clearTimeout(settleTimer)
        settleTimer = setTimeout(async () => {
          try {
            renderer.setHybrid({ shadowSamples: 4 })
            await renderer.drawFrame()
          } catch (error) {
            const report = onError ?? console.error
            report(error)
          }
        }, 180)
      },
    })
    const probes = attachShowroomProbes(canvas, renderer, renderer.getHybrid().probeBounds)
    const draw = renderer.drawFrame.bind(renderer)
    renderer.drawFrame = async () => {
      const result = await draw()
      if (result === 'rendered') probes.update()
      return result
    }
    const dispose = renderer.dispose.bind(renderer)
    renderer.dispose = () => {
      clearTimeout(settleTimer)
      probes.dispose()
      dispose()
    }
    return renderer
  } catch (error) {
    renderer.dispose()
    throw error
  }
}
