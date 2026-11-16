import * as Nova from '../src/Nova.js'
import { showRasterLaboratory } from './RasterLaboratory.js'
import { addPostProcessingControls } from './PostProcessingControls.js'
export async function showPlanarReflections(canvas, shaderDirectory = '/generated/', options = {}) {
  return showRasterLaboratory(canvas, shaderDirectory, {
    ...options,
    createView: Nova.createPlanarReflectionView,
    initialPlaying: false,
    initialScale: 0.75,
    showChecker: false,
    showSplit: false,
    panelLabel: 'Planar reflection parameters',
    describe: () => 'Planar · single bounce · ',
    extraControls: (api) => {
      const { view, control, controller, changed } = api
      controller.getPlanarReflections = () => view.getPlanarReflections()
      controller.setPlanarReflections = (v) => {
        view.setPlanarReflections(v)
        changed()
      }
      const set = (key, value) => controller.setPlanarReflections({ [key]: value })
      for (const [prefix, label, aria] of [
        ['floor', 'Floor', 'Floor'],
        ['mirror', 'Mirror', 'Mirror'],
      ]) {
        control(
          label + ' planar reflection',
          aria + ' planar reflection',
          'checkbox',
          () => view.getPlanarReflections()[prefix + 'Enabled'],
          (v) => set(prefix + 'Enabled', v),
        )
        control(
          label + ' roughness',
          aria + ' roughness',
          'range',
          () => view.getPlanarReflections()[prefix + 'Roughness'],
          (v) => set(prefix + 'Roughness', v),
          [0, 1, 0.01],
        )
        control(
          label + ' intensity',
          aria + ' reflection intensity',
          'range',
          () => view.getPlanarReflections()[prefix + 'Intensity'],
          (v) => set(prefix + 'Intensity', v),
          [0, 2, 0.05],
        )
      }
      let environment = 'builtin',
        loading = false
      control(
        'Environment',
        'Reflection environment',
        'select',
        () => environment,
        async (v) => {
          loading = true
          changed()
          try {
            if (v === 'builtin') view.useDefaultEnvironment()
            else await view.loadEnvironment(shaderDirectory + v + '.bin')
            environment = v
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
