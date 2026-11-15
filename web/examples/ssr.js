import * as Nova from '../src/Nova.js'
import { showRasterLaboratory } from './RasterLaboratory.js'
import { addPostProcessingControls } from './PostProcessingControls.js'
export async function showScreenSpaceReflections(
  canvas,
  shaderDirectory = '/generated/',
  options = {},
) {
  return showRasterLaboratory(canvas, shaderDirectory, {
    ...options,
    createView: Nova.createScreenSpaceReflectionView,
    initialPlaying: false,
    initialScale: 0.75,
    showChecker: false,
    showSplit: false,
    panelLabel: 'SSR 参数',
    describe: (view) => {
      const s = view.getScreenSpaceReflections()
      return `${s.enabled ? 'SSR' : 'Environment only'} · ${s.hierarchical ? 'Hi-Z' : 'Linear'} · ${s.debug} · `
    },
    extraControls: (api) => {
      const { view, control, controller, changed } = api
      controller.getScreenSpaceReflections = () => view.getScreenSpaceReflections()
      controller.setScreenSpaceReflections = (v) => {
        view.setScreenSpaceReflections(v)
        changed()
      }
      const set = (key, v) => controller.setScreenSpaceReflections({ [key]: v })
      control(
        'SSR',
        'SSR 开关',
        'checkbox',
        () => view.getScreenSpaceReflections().enabled,
        (v) => set('enabled', v),
      )
      control(
        'Depth hierarchy',
        '深度金字塔',
        'checkbox',
        () => view.getScreenSpaceReflections().hierarchical,
        (v) => set('hierarchical', v),
      )
      control(
        'Output',
        '反射调试视图',
        'select',
        () => view.getScreenSpaceReflections().debug,
        (v) => set('debug', v),
        [
          ['composite', 'Composite'],
          ['reflections', 'Reflections'],
          ['hit-mask', 'Hit mask'],
          ['normals', 'World normals'],
          ['roughness', 'Roughness'],
          ['linear-depth', 'Linear depth'],
          ['depth-pyramid', 'Depth pyramid'],
        ],
      )
      for (const [key, label, aria, min, max, step] of [
        ['maxSteps', 'Max steps', '最大追踪步数', 16, 256, 16],
        ['maxDistance', 'Max distance', '最大追踪距离', 1, 60, 1],
        ['thickness', 'Thickness', '厚度阈值', 0.01, 1, 0.01],
        ['stride', 'Linear stride', '线性步长', 0.5, 4, 0.5],
        ['roughnessCutoff', 'Roughness cutoff', '粗糙度上限', 0.05, 1, 0.05],
        ['edgeFade', 'Edge fade', '屏幕边缘淡出', 0.005, 0.3, 0.005],
        ['intensity', 'Reflection intensity', '反射强度', 0, 2, 0.05],
        ['environmentIntensity', 'Environment intensity', '环境反射强度', 0, 4, 0.05],
        ['debugMip', 'Depth mip', '深度层级', 0, 12, 1],
      ])
        control(
          label,
          aria,
          'range',
          () => view.getScreenSpaceReflections()[key],
          (v) => set(key, v),
          [min, max, step],
          () =>
            key === 'stride'
              ? !view.getScreenSpaceReflections().hierarchical
              : key === 'debugMip'
                ? view.getScreenSpaceReflections().debug === 'depth-pyramid'
                : true,
        )
      let environment = 'builtin',
        loading = false
      controller.loadEnvironment = async (value) => {
        loading = true
        changed()
        try {
          if (value === 'builtin') view.useDefaultEnvironment()
          else await view.loadEnvironment(shaderDirectory + value + '.bin')
          environment = value
        } finally {
          loading = false
          changed()
        }
      }
      control(
        'Environment',
        '反射环境',
        'select',
        () => environment,
        (v) => controller.loadEnvironment(v),
        [
          ['builtin', 'Studio (procedural HDR)'],
          ['venice_sunset', 'Venice sunset'],
          ['kloppenheim_06', 'Kloppenheim'],
          ['studio_small_09', 'Studio small'],
        ],
        () => !loading,
      )
      addPostProcessingControls(api)
    },
  })
}
