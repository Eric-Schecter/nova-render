export const modelRasterPipelines = ['forward', 'deferred']
export const gBufferViews = [
  'final',
  'base-color',
  'world-normal',
  'geometric-normal',
  'linear-depth',
  'roughness',
  'metallic',
  'occlusion',
  'emissive',
  'world-position',
]
export const defaultRasterSettings = () => ({ pipeline: 'forward', view: 'final', debugRange: 20 })
export function copyRasterSettings(value) {
  const s = { ...value }
  if (
    !modelRasterPipelines.includes(s.pipeline) ||
    !gBufferViews.includes(s.view) ||
    !Number.isFinite(s.debugRange) ||
    s.debugRange <= 0 ||
    s.debugRange > 1000000
  )
    throw Error('Invalid model raster settings; only Forward and Deferred are supported')
  if (s.pipeline === 'forward' && s.view !== 'final') throw Error('G-buffer views require Deferred')
  return s
}
export function rasterCapabilities() {
  return {
    forward: true,
    deferred: true,
    gBuffer: true,
    forwardPlus: false,
    clusteredForward: false,
    deferredMSAA: false,
    opaqueOnly: true,
    deferredRequiresModel: true,
    debugBypassesAAAndPost: true,
    gBufferTargets: 4,
    gBufferBytesPerPixel: 32,
  }
}
