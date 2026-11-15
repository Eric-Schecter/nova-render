import * as Nova from '../src/Nova.js'
import { showRasterLaboratory } from './RasterLaboratory.js'
import { addPostProcessingControls } from './PostProcessingControls.js'
export async function showReflectionProbe(canvas, shaderDirectory = '/generated/', options = {}) {
  return showRasterLaboratory(canvas, shaderDirectory, {
    ...options,
    createView: Nova.createReflectionProbeView,
    initialPlaying: false,
    initialScale: 0.75,
    showChecker: false,
    showSplit: false,
    panelLabel: '反射探针参数',
    describe: (view) =>
      `Probe · ${view.getReflectionProbe().boxProjection ? 'Box projection' : 'Uncorrected'} · ${view.getProbeCaptureCount()} captures · `,
    extraControls: (api) => {
      const { view, control, controller, changed } = api
      controller.getReflectionProbe = () => view.getReflectionProbe()
      controller.setReflectionProbe = (value) => {
        view.setReflectionProbe(value)
        changed()
      }
      controller.updateProbe = () => {
        view.updateProbe()
        changed()
      }
      const set = (key, value) => controller.setReflectionProbe({ [key]: value })
      control(
        'Local cubemap',
        '局部 Cubemap',
        'checkbox',
        () => view.getReflectionProbe().enabled,
        (v) => set('enabled', v),
      )
      control(
        'Box projection',
        '盒体投影修正',
        'checkbox',
        () => view.getReflectionProbe().boxProjection,
        (v) => set('boxProjection', v),
      )
      for (const [key, label, aria, max, step] of [
        ['roughness', 'Roughness', '反射粗糙度', 1, 0.01],
        ['intensity', 'Reflection intensity', '反射强度', 2, 0.05],
        ['blendDistance', 'Boundary blend', '边界混合距离', 2, 0.05],
      ])
        control(
          label,
          aria,
          'range',
          () => view.getReflectionProbe()[key],
          (v) => set(key, v),
          [0, max, step],
        )
      for (const [index, axis, min, max] of [
        [0, 'X', -3, 3],
        [1, 'Y', -0.5, 2.5],
        [2, 'Z', -3, 3],
      ])
        control(
          'Probe ' + axis,
          '探针位置 ' + axis,
          'range',
          () => view.getReflectionProbe().position[index],
          (v) => {
            const position = view.getReflectionProbe().position
            position[index] = v
            set('position', position)
          },
          [min, max, 0.1],
        )
      control(
        'Output',
        '探针调试视图',
        'select',
        () => view.getReflectionProbe().debug,
        (v) => set('debug', v),
        [
          ['composite', 'Composite'],
          ['reflections', 'Reflections'],
          ['influence', 'Influence'],
          ['cubemap', 'Cubemap (six faces + roughness)'],
        ],
      )
      control(
        'Refresh probe',
        '刷新反射探针',
        'button',
        () => 'Refresh probe',
        () => controller.updateProbe(),
      )
      let environment = 'builtin',
        loading = false
      control(
        'Environment',
        '反射环境',
        'select',
        () => environment,
        async (value) => {
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
        },
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
