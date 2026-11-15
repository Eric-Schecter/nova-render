import * as Nova from '../src/Nova.js'
import { showRasterLaboratory } from './RasterLaboratory.js'
export async function showPostProcessing(canvas, shaderDirectory = '/generated/', options = {}) {
  return showRasterLaboratory(canvas, shaderDirectory, {
    ...options,
    createView: Nova.createPostProcessingView,
    initialPlaying: false,
    initialScale: 1,
    showChecker: false,
    showSplit: false,
    panelLabel: '后处理参数',
    describe: (view) => {
      const p = view.getPostProcessing()
      return `${p.enabled ? { linear: 'Linear', reinhard: 'Reinhard', aces: 'ACES fit' }[p.toneMapping] : 'Bypass'} · ${p.autoExposure ? 'Auto exposure' : 'Manual exposure'} · `
    },
    extraControls: ({ view, control, controller, changed }) => {
      controller.getPostProcessing = () => view.getPostProcessing()
      controller.setPostProcessing = (v) => {
        view.setPostProcessing(v)
        changed()
      }
      controller.getSceneIntensity = () => view.getSceneIntensity()
      controller.setSceneIntensity = (v) => {
        view.setSceneIntensity(v)
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
        '后处理开关',
        'checkbox',
        () => view.getPostProcessing().enabled,
        (v) => set('enabled', v),
      )
      control(
        'Tone mapping',
        '色调映射',
        'select',
        () => view.getPostProcessing().toneMapping,
        (v) => set('toneMapping', v),
        [
          ['linear', 'Linear'],
          ['reinhard', 'Reinhard'],
          ['aces', 'ACES fit'],
        ],
      )
      control(
        'Auto exposure',
        '自动曝光',
        'checkbox',
        () => view.getPostProcessing().autoExposure,
        (v) => set('autoExposure', v),
      )
      control(
        'Bloom',
        '泛光开关',
        'checkbox',
        () => view.getPostProcessing().bloom,
        (v) => set('bloom', v),
      )
      control(
        'Scene intensity',
        '场景亮度',
        'range',
        () => view.getSceneIntensity(),
        (v) => controller.setSceneIntensity(v),
        [0.05, 20, 0.05],
      )
      for (const [key, label, aria, min, max, step] of [
        ['exposureEV', 'Exposure / compensation EV', '曝光补偿', -8, 8, 0.1],
        ['minEV', 'Min auto EV', '最低自动曝光', -10, 10, 0.1],
        ['maxEV', 'Max auto EV', '最高自动曝光', -10, 10, 0.1],
        ['adaptationRate', 'Adaptation rate', '适应速度', 0.1, 10, 0.1],
        ['bloomIntensity', 'Bloom intensity', '泛光强度', 0, 2, 0.01],
        ['bloomThreshold', 'Bloom threshold', '泛光阈值', 0, 20, 0.1],
        ['bloomKnee', 'Bloom soft knee', '泛光过渡', 0, 1, 0.05],
        ['saturation', 'Saturation', '饱和度', 0, 2, 0.05],
        ['contrast', 'Contrast', '对比度', 0.1, 2, 0.05],
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
    },
  })
}
