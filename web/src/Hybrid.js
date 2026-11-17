export const defaultHybrid = () => ({
  shadows: true,
  shadowSamples: 4,
  view: 'final',
  shadowLight: 0,
})
export function copyHybrid(value) {
  if (
    !value ||
    typeof value !== 'object' ||
    Object.keys(value).some((k) => !['shadows', 'shadowSamples', 'view', 'shadowLight'].includes(k))
  )
    throw Error('Unknown Hybrid setting')
  const s = { ...defaultHybrid(), ...value }
  if (
    typeof s.shadows !== 'boolean' ||
    !Number.isInteger(s.shadowSamples) ||
    s.shadowSamples < 1 ||
    s.shadowSamples > 16 ||
    !['final', 'shadows'].includes(s.view) ||
    !Number.isInteger(s.shadowLight) ||
    s.shadowLight < 0 ||
    s.shadowLight > 15
  )
    throw Error('Invalid Hybrid settings')
  return s
}
export function hybridCapabilities(animated = false) {
  return {
    supported: !animated,
    rasterPrimaryVisibility: true,
    shadows: true,
    softwareRayTracing: true,
    hardwareRayTracing: false,
    ambientOcclusion: false,
    reflections: false,
    globalIllumination: false,
    temporalReconstruction: false,
    animatedGeometry: false,
    taa: false,
    splitAA: false,
    gpuPresentation: true,
    alphaMask: true,
    alphaBlend: true,
    execution: 'fragment-bvh',
    reason: animated ? 'Hybrid currently requires static geometry' : '',
  }
}
