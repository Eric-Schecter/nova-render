import { createAntiAliasingView } from './AntiAliasingView.js'
export function createReflectionProbeView(options = {}) {
  return createAntiAliasingView({ ...options, reflectionScene: true, probeScene: true })
}
