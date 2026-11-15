import { createAntiAliasingView } from './AntiAliasingView.js'
export async function createScreenSpaceReflectionView(options = {}) {
  return createAntiAliasingView({ ...options, reflectionScene: true })
}
