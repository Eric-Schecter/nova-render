// Application scene presets; all rendering uses Nova's public scene/camera API.
import { attachOrbitControls } from '../src/OrbitControls.js'
import { cameraBasis, cameraFromOrbit } from '../src/Camera.js'
export async function createHybridScenes(renderer, canvas, shaderDirectory, options = {}) {
  const directory = new URL('../hybrid-reference/', new URL(shaderDirectory, location.href)),
    response = await fetch(new URL('scenes.json', directory))
  if (!response.ok) throw Error('Reference scene resources are unavailable')
  const reference = await response.json(),
    scenes = [
      { id: 'box', name: 'Box and mirror', cameras: [{ id: 'default', name: 'Default' }] },
      ...reference.scenes,
    ]
  let current,
    currentView,
    orbit,
    lens,
    orbitControls,
    previousReflectionSettings,
    disposed = false,
    loading = false
  const check = () => {
    if (disposed) throw Error('Hybrid scene controller disposed')
  }
  function setView(id) {
    check()
    const preset = current.cameras.find((c) => c.id === id)
    if (!preset) throw Error('Unknown camera preset')
    orbitControls?.dispose()
    currentView = id
    if (current.id === 'box') {
      orbit = { yaw: 30, pitch: 25, distance: 5.5, target: [0, 0, 0], exposure: 1 }
      lens = cameraFromOrbit(orbit)
    } else {
      const { id: _, ...camera } = preset
      lens = camera
      const { forward } = cameraBasis(camera),
        distance = Math.min(90, Math.max(3, Math.hypot(...camera.position) * 0.7))
      orbit = {
        yaw: (Math.atan2(-forward[0], -forward[2]) * 180) / Math.PI,
        pitch: (Math.asin(-forward[1]) * 180) / Math.PI,
        distance,
        exposure: camera.exposure,
        target: camera.position.map((v, k) => v + forward[k] * distance),
      }
    }
    renderer.setCamera(orbit)
    renderer.setViewCamera(lens)
    renderer.resetHybridHistory()
    // Preserve the reference lens while orbiting; no backend-specific calls.
    orbitControls = attachOrbitControls(canvas, {
      getCamera: () => renderer.getCamera(),
      setCamera: (value) => {
        const previous = renderer.getViewCamera()
        orbit = value
        renderer.setCamera(value)
        renderer.setViewCamera({
          ...cameraFromOrbit(value),
          projection: previous.projection,
          verticalFov: previous.verticalFov,
          orthographicHeight: previous.orthographicHeight,
          nearPlane: previous.nearPlane,
          farPlane: previous.farPlane,
        })
      },
      drawFrame: () => renderer.drawFrame(),
      onChange: options.onChange,
      onError: options.onError,
    })
  }
  async function selectScene(id) {
    check()
    if (loading) throw Error('A scene is already loading')
    const next = scenes.find((s) => s.id === id)
    if (!next) throw Error('Unknown Hybrid scene')
    loading = true
    try {
      if (id === 'box') {
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
          [
            { id: 'box', url: shaderDirectory + 'SceneRed.glb' },
            { id: 'floor', url: shaderDirectory + 'SceneRed.glb' },
          ],
          [
            { id: 'ground', assetId: 'floor', world: world(2, 0.06, 2, 0, -0.8) },
            { id: 'caster', assetId: 'box', world: world(0.3, 1, 0.3) },
          ],
        )
        const material = {
          ...renderer.getMaterials('box')[0],
          baseColor: [0.8, 0.8, 0.8, 1],
          metallic: 0,
          roughness: 0.7,
        }
        await renderer.setMaterial(0, material, 'box')
        await renderer.setMaterial(0, { ...material, metallic: 0.9, roughness: 0.08 }, 'floor')
      } else {
        await renderer.setScene(
          next.assets.map((a) => ({ ...a, url: new URL(a.url, directory).href })),
          next.instances,
        )
      }
      check()
      const initial = !current
      if (next.environment || current?.environment) {
        await renderer.loadEnvironment(
          next.environment ? new URL(next.environment, directory).href : null,
        )
        check()
      }
      if (id === 'reflections-test' && current?.id !== id) {
        const { reflectionSamples, halfResolutionReflections, reflectionDenoising } =
          renderer.getHybrid()
        const { intensity } = renderer.getEnvironment()
        previousReflectionSettings = {
          reflectionSamples,
          halfResolutionReflections,
          reflectionDenoising,
          intensity,
        }
        const canDenoise = renderer.getHybridCapabilities().halfResolutionReflections
        renderer.setHybrid({
          reflectionSamples: 2,
          halfResolutionReflections: canDenoise,
          reflectionDenoising: canDenoise,
        })
        renderer.setEnvironment({ ...renderer.getEnvironment(), intensity: 0.78 })
      } else if (id !== 'reflections-test' && previousReflectionSettings) {
        const { intensity, ...hybrid } = previousReflectionSettings
        renderer.setHybrid(hybrid)
        renderer.setEnvironment({ ...renderer.getEnvironment(), intensity })
        previousReflectionSettings = undefined
      }
      current = next
      renderer.setLighting('pbr')
      renderer.setDirectionalLight(
        id === 'box'
          ? { direction: [-1, 2, 1], color: [1, 1, 1], intensity: 4 }
          : (next.light ?? reference.light),
      )
      renderer.setHybrid({ aoRadius: id === 'box' ? 1 : 2 })
      if (initial) renderer.setRenderMode('hybrid')
      setView(next.cameras[0].id)
      await renderer.drawFrame()
    } finally {
      loading = false
    }
  }
  return {
    choices: scenes.map(({ id, name }) => ({ id, name })),
    get scene() {
      return current?.id
    },
    get view() {
      return currentView
    },
    get views() {
      return current?.cameras.map((c, i) => ({ id: c.id, name: c.name || 'View ' + (i + 1) })) || []
    },
    selectScene,
    async selectView(id) {
      setView(id)
      await renderer.drawFrame()
    },
    dispose() {
      disposed = true
      orbitControls?.dispose()
    },
  }
}
