import { createRenderer, createReflectionProbeView } from '@nova/render-web'
window.validation = (async () => {
  const renderer = await createRenderer({
    canvas: document.querySelector('#triangle'),
    shaderDirectory: '/nova/',
  })
  const shader = await fetch('/nova/triangle.json').then((r) => r.json())
  await renderer.setShader(shader)
  await renderer.drawFrame()
  const view = await createReflectionProbeView({
    canvas: document.querySelector('#probes'),
    shaderDirectory: '/nova/',
  })
  view.setReflectionProbes([
    { position: [-2, 0.6, 0], influenceMax: [1, 3, 4] },
    { position: [2, 0.6, 0], influenceMin: [-1, -1, -4] },
  ])
  await view.drawFrame(256, 192)
  const image = await view.readPixels(),
    values = new Uint16Array(image.pixels.buffer)
  if (view.getProbeCaptureCount() !== 2 || !values.some((v) => v !== values[0]))
    throw Error('Installed reflection API failed')
  const assets = [{ id: 'box', url: '/models/Red.glb', sourceSpace: true }]
  const instances = [
    { id: 'left', assetId: 'box' },
    { id: 'right', assetId: 'box', world: [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -4, 0, 0, 1] },
  ]
  await renderer.loadModel('/models/Dual.glb')
  if (renderer.getMaterials().length !== 2) throw Error('Installed npm multislot loading failed')
  const material = renderer.getMaterials()[0]
  material.baseColor = [0.2, 0.8, 0.3, 1]
  material.roughness = 0.25
  material.metallic = 0.7
  material.baseColorTexture = { width: 1, height: 1, pixels: new Uint8Array([255, 255, 255, 255]) }
  await renderer.setMaterial(0, material)
  if (renderer.getMaterials()[0].roughness !== 0.25)
    throw Error('Installed npm material API failed')
  renderer.setFog({ mode: 'off' })
  renderer.setShadows({ enabled: true, softness: 0 })
  await renderer.loadEnvironment(null)
  if (!renderer.getShadows().enabled || renderer.getFog().mode !== 'off')
    throw Error('Installed atmosphere API failed')
  const lightCaps = renderer.getLightingCapabilities()
  if (
    !lightCaps.point ||
    !lightCaps.spot ||
    lightCaps.maxLights !== 16 ||
    !lightCaps.pointShadows ||
    !lightCaps.spotShadows
  )
    throw Error('Installed light capabilities mismatch')
  renderer.setLighting('pbr')
  renderer.setLights([
    { id: 'point', type: 'point', position: [1, 2, 3], intensity: 8, castShadow: true },
    { id: 'spot', type: 'spot', position: [-2, 3, 1], direction: [0, -1, -1], intensity: 8 },
  ])
  if (renderer.getLights().length !== 2) throw Error('Installed light collection missing')
  await renderer.setScene(assets, instances)
  const ray = { origin: [2, 1, 5], direction: [0, 0, -2] }
  const picked = renderer.pickScene(ray)
  if (
    !picked ||
    picked.id !== 'left' ||
    picked.distance !== 4 ||
    picked.position[2] !== 1 ||
    picked.normal[2] !== 1
  )
    throw Error('Installed picking API failed')
  if (renderer.pickScene({ ...ray, excludeIds: ['left'] }))
    throw Error('Installed picking exclusion failed')
  if (!renderer.pickScene({ ...ray, includeIds: ['left'] }))
    throw Error('Installed picking inclusion failed')

  if (renderer.getSceneAssetBounds('box')?.maximum[0] !== 4)
    throw Error('Installed scene source space failed')
  renderer.setAntiAliasing({ method: 'taa' })
  await renderer.drawFrame()
  await renderer.setScene([], [])
  if (renderer.pickScene(ray)) throw Error('Installed picking clear failed')
  await renderer.drawFrame()
  if (renderer.getSceneAssetBounds('box') !== null) throw Error('Installed scene clear failed')
  await renderer.setScene(assets, instances)
  await renderer.drawFrame()
  renderer.setViewCamera({
    projection: 'orthographic',
    orthographicHeight: 5,
    verticalFov: 60,
    nearPlane: 0.1,
    farPlane: 100,
    orientation: [0, 0, 0.38268343, 0.9238795],
  })
  renderer.setViewport({ x: 20, y: 15, width: 160, height: 120 })
  await renderer.drawFrame()
  if (
    renderer.getViewport().width !== 160 ||
    renderer.getViewCamera().projection !== 'orthographic'
  )
    throw Error('Installed camera/viewport API failed')
  renderer.setViewport(null)
  await renderer.drawFrame()
  document.querySelector('#status').textContent =
    'PASS installed npm package: triangle, two probes, scene instances, original model space'
  window.disposeNova = () => {
    renderer.dispose()
    view.dispose()
  }
  return { captures: view.getProbeCaptureCount(), bytes: image.pixels.byteLength }
})()
window.validation.catch((error) => (document.querySelector('#status').textContent = error.message))
