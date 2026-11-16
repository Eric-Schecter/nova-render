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
  document.querySelector('#status').textContent =
    'PASS installed npm package: triangle, two probes, image readback'
  window.disposeNova = () => {
    renderer.dispose()
    view.dispose()
  }
  return { captures: view.getProbeCaptureCount(), bytes: image.pixels.byteLength }
})()
window.validation.catch((error) => (document.querySelector('#status').textContent = error.message))
