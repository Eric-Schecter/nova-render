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
  await renderer.setScene(assets, instances)
  if (renderer.getSceneAssetBounds('box')?.maximum[0] !== 4)
    throw Error('Installed scene source space failed')
  renderer.setAntiAliasing({ method: 'taa' })
  await renderer.drawFrame()
  await renderer.setScene([], [])
  await renderer.drawFrame()
  if (renderer.getSceneAssetBounds('box') !== null) throw Error('Installed scene clear failed')
  await renderer.setScene(assets, instances)
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
