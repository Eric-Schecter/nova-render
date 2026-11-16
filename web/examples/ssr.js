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
    panelLabel: 'SSR parameters',
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
        'SSR enabled',
        'checkbox',
        () => view.getScreenSpaceReflections().enabled,
        (v) => set('enabled', v),
      )
      control(
        'Depth hierarchy',
        'Depth hierarchy',
        'checkbox',
        () => view.getScreenSpaceReflections().hierarchical,
        (v) => set('hierarchical', v),
      )
      control(
        'Output',
        'Reflection output',
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
        ['maxSteps', 'Max steps', 'Maximum trace steps', 16, 256, 16],
        ['maxDistance', 'Max distance', 'Maximum trace distance', 1, 60, 1],
        ['thickness', 'Thickness', 'Thickness', 0.01, 1, 0.01],
        ['stride', 'Linear stride', 'Linear stride', 0.5, 4, 0.5],
        ['roughnessCutoff', 'Roughness cutoff', 'Roughness cutoff', 0.05, 1, 0.05],
        ['edgeFade', 'Edge fade', 'Screen edge fade', 0.005, 0.3, 0.005],
        ['intensity', 'Reflection intensity', 'Reflection intensity', 0, 2, 0.05],
        ['environmentIntensity', 'Environment intensity', 'Environment intensity', 0, 4, 0.05],
        ['debugMip', 'Depth mip', 'Depth mip', 0, 12, 1],
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
        'Reflection environment',
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
