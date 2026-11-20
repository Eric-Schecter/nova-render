export const defaultHybrid = () => ({
  shadows: true,
  shadowSamples: 4,
  shadowTemporalReconstruction: false,
  view: 'final',
  shadowLight: 0,
  temporalReconstruction: false,
  historyWeight: 0.9,
  reflections: false,
  diffuseBounce: false,
  probeGI: false,
  probeBounds: null,
  reflectionDistance: 1000,
  reflectionStrength: 1,
  reflectionSamples: 1,
  halfResolutionReflections: false,
  reflectionDenoising: false,
  reflectionHitReprojection: false,
  clearcoatReflections: true,
  ambientOcclusion: false,
  aoRadius: 1,
  aoStrength: 1,
  aoSamples: 4,
})
export function copyHybrid(value) {
  if (
    !value ||
    typeof value !== 'object' ||
    Object.keys(value).some(
      (k) =>
        ![
          'shadows',
          'shadowSamples',
          'shadowTemporalReconstruction',
          'view',
          'shadowLight',
          'temporalReconstruction',
          'historyWeight',
          'reflections',
          'diffuseBounce',
          'probeGI',
          'probeBounds',
          'reflectionDistance',
          'reflectionStrength',
          'reflectionSamples',
          'halfResolutionReflections',
          'reflectionDenoising',
          'reflectionHitReprojection',
          'clearcoatReflections',
          'ambientOcclusion',
          'aoRadius',
          'aoStrength',
          'aoSamples',
        ].includes(k),
    )
  )
    throw Error('Unknown Hybrid setting')
  const s = { ...defaultHybrid(), ...value }
  if (
    typeof s.shadows !== 'boolean' ||
    typeof s.shadowTemporalReconstruction !== 'boolean' ||
    !Number.isInteger(s.shadowSamples) ||
    s.shadowSamples < 1 ||
    s.shadowSamples > 16 ||
    !['final', 'shadows', 'ambient-occlusion', 'reflections'].includes(s.view) ||
    !Number.isInteger(s.shadowLight) ||
    s.shadowLight < 0 ||
    s.shadowLight > 15 ||
    typeof s.ambientOcclusion !== 'boolean' ||
    !Number.isFinite(s.aoRadius) ||
    s.aoRadius <= 0 ||
    s.aoRadius > 10000 ||
    !Number.isFinite(s.aoStrength) ||
    s.aoStrength < 0 ||
    s.aoStrength > 4 ||
    !Number.isInteger(s.aoSamples) ||
    s.aoSamples < 1 ||
    s.aoSamples > 32 ||
    typeof s.reflections !== 'boolean' ||
    typeof s.diffuseBounce !== 'boolean' ||
    typeof s.probeGI !== 'boolean' ||
    !Number.isFinite(s.reflectionDistance) ||
    s.reflectionDistance <= 0 ||
    s.reflectionDistance > 100000 ||
    !Number.isFinite(s.reflectionStrength) ||
    s.reflectionStrength < 0 ||
    s.reflectionStrength > 1 ||
    typeof s.halfResolutionReflections !== 'boolean' ||
    typeof s.reflectionDenoising !== 'boolean' ||
    typeof s.reflectionHitReprojection !== 'boolean' ||
    typeof s.clearcoatReflections !== 'boolean' ||
    !Number.isInteger(s.reflectionSamples) ||
    s.reflectionSamples < 1 ||
    s.reflectionSamples > 16 ||
    typeof s.temporalReconstruction !== 'boolean' ||
    !Number.isFinite(s.historyWeight) ||
    s.historyWeight < 0 ||
    s.historyWeight > 0.98
  )
    throw Error('Invalid Hybrid settings')
  if (s.probeBounds !== null) {
    const { minimum, maximum } = s.probeBounds || {}
    if (
      ![minimum, maximum].every(
        (values) =>
          Array.isArray(values) &&
          values.length === 3 &&
          values.every((v) => Number.isFinite(v) && Number.isFinite(Math.fround(v))),
      ) ||
      minimum.some(
        (low, i) =>
          Math.fround(low) >= Math.fround(maximum[i]) ||
          !Number.isFinite(Math.fround(maximum[i] - low)),
      )
    )
      throw Error('Invalid Hybrid probe bounds')
    s.probeBounds = { minimum: [...minimum], maximum: [...maximum] }
  }
  return s
}
export function hybridCapabilities(animated = false) {
  return {
    supported: !animated,
    rasterPrimaryVisibility: true,
    shadows: true,
    softwareRayTracing: true,
    hardwareRayTracing: false,
    ambientOcclusion: true,
    reflections: true,
    globalIllumination: true,
    probeGI: !animated,
    temporalReconstruction: true,
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
