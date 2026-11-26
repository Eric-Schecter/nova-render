import { defaultPostProcessing } from './PostProcessing.js'

export function productShowcaseAntiAliasing(interacting = false) {
  return {
    method: interacting ? 'fxaa' : 'ssaa',
    historyWeight: 0.9,
    zoom: 1,
    split: false,
    textureFiltering: true,
  }
}

export function makeProductShowcaseSettings(interacting = false) {
  return {
    postProcessing: {
      ...defaultPostProcessing(),
      enabled: true,
      toneMapping: 'aces-filmic',
      bloomThreshold: 1.82,
      bloomKnee: 0.67,
      bloomIntensity: 0.12,
    },
    shadows: { enabled: true, softness: 1, samples: 16, bias: 0.002 },
    antiAliasing: productShowcaseAntiAliasing(interacting),
  }
}
