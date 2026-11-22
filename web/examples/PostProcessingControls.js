export function addPostProcessingControls({ view, control, controller, changed }) {
  controller.getPostProcessing = () => view.getPostProcessing()
  controller.setPostProcessing = (v) => {
    view.setPostProcessing(v)
    changed()
  }
  const set = (key, v) => {
    const next = { ...view.getPostProcessing(), [key]: v }
    if (key === 'minEV') next.maxEV = Math.max(v, next.maxEV)
    if (key === 'maxEV') next.minEV = Math.min(v, next.minEV)
    controller.setPostProcessing(next)
  }
  control(
    'Post-processing',
    'Post-processing enabled',
    'checkbox',
    () => view.getPostProcessing().enabled,
    (v) => set('enabled', v),
  )
  control(
    'Tone mapping',
    'Tone mapping',
    'select',
    () => view.getPostProcessing().toneMapping,
    (v) => set('toneMapping', v),
    [
      ['linear', 'Linear'],
      ['reinhard', 'Reinhard'],
      ['aces', 'ACES fit'],
      ['agx', 'AgX'],
    ],
  )
  control(
    'Auto exposure',
    'Auto exposure',
    'checkbox',
    () => view.getPostProcessing().autoExposure,
    (v) => set('autoExposure', v),
  )
  control(
    'Bloom',
    'Bloom enabled',
    'checkbox',
    () => view.getPostProcessing().bloom,
    (v) => set('bloom', v),
  )
  for (const [key, label, aria, min, max, step] of [
    ['exposureEV', 'Exposure / compensation EV', 'Exposure compensation', -8, 8, 0.1],
    ['minEV', 'Min auto EV', 'Minimum auto EV', -10, 10, 0.1],
    ['maxEV', 'Max auto EV', 'Maximum auto EV', -10, 10, 0.1],
    ['adaptationRate', 'Adaptation rate', 'Adaptation rate', 0.1, 10, 0.1],
    ['bloomIntensity', 'Bloom intensity', 'Bloom intensity', 0, 2, 0.01],
    ['bloomThreshold', 'Bloom threshold', 'Bloom threshold', 0, 20, 0.1],
    ['bloomKnee', 'Bloom soft knee', 'Bloom soft knee', 0, 1, 0.05],
    ['saturation', 'Saturation', 'Saturation', 0, 2, 0.05],
    ['contrast', 'Contrast', 'Contrast', 0.1, 2, 0.05],
  ])
    control(
      label,
      aria,
      'range',
      () => view.getPostProcessing()[key],
      (v) => set(key, v),
      [min, max, step],
      () => {
        const p = view.getPostProcessing()
        if (!p.enabled) return false
        if (['minEV', 'maxEV', 'adaptationRate'].includes(key)) return p.autoExposure
        if (key.startsWith('bloom')) return p.bloom
        return true
      },
    )
}
