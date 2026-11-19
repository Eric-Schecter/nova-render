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
export const deferredLimits = {
  maxColorAttachmentBytesPerSample: 40,
  maxSampledTexturesPerShaderStage: 20,
}
export function canUseDeferred(limits) {
  return Object.entries(deferredLimits).every(([key, value]) => limits[key] >= value)
}
export const halfReflectionTextureLimit = deferredLimits.maxSampledTexturesPerShaderStage + 1
export function canUseHalfReflections(limits) {
  return (
    canUseDeferred(limits) && limits.maxSampledTexturesPerShaderStage >= halfReflectionTextureLimit
  )
}
export function rasterCapabilities(deferred = true) {
  return {
    forward: true,
    deferred,
    gBuffer: deferred,
    forwardPlus: false,
    clusteredForward: false,
    deferredMSAA: false,
    opaqueOnly: false,
    deferredRequiresModel: true,
    debugBypassesAAAndPost: true,
    gBufferTargets: deferred ? 5 : 0,
    gBufferBytesPerPixel: deferred ? 40 : 0,
  }
}
