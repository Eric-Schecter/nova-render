import { showEnvironment } from './environment.js'

// Reuse Nova's model, environment and camera API; no direct WebGPU calls.
export async function showWeekend(canvas, modelURL, shaderDirectory, camera, options) {
  const renderer = await showEnvironment(canvas, modelURL, shaderDirectory, camera, options)
  try {
    renderer.setRayTracing({ ...renderer.getRayTracing(), maxBounces: 8 })
    renderer.setRenderMode('ray-tracing')
    await renderer.drawFrame()
    return renderer
  } catch (error) {
    renderer.dispose()
    throw error
  }
}
