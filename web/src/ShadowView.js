import { createAntiAliasingView } from './AntiAliasingView.js'
// GPU shadow-map rasterization, shading and AA are all generated from the native HLSL.
export async function createShadowView(options = {}) {
  return createAntiAliasingView({ ...options, shadowScene: true })
}
