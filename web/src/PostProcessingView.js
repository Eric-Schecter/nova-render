import { createAntiAliasingView } from './AntiAliasingView.js'
export async function createPostProcessingView(options = {}) {
  return createAntiAliasingView({ ...options, hdrScene: true })
}
