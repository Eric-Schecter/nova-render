import { createAntiAliasingView } from './AntiAliasingView.js'
export function createPlanarReflectionView(options = {}) {
  return createAntiAliasingView({ ...options, reflectionScene: true, planarScene: true })
}
