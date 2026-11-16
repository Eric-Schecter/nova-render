import * as Nova from '../src/Nova.js'
import { showRasterLaboratory } from './RasterLaboratory.js'
import { addPostProcessingControls } from './PostProcessingControls.js'
export function roomProbes() {
  return [0, 1, 2].map((i) => ({
    enabled: true,
    intensity: 1,
    blendDistance: 1.2,
    position: [-2.5 + i * 2.5, 0.6, -1],
    boxMin: [-4, -1, -4],
    boxMax: [4, 3, 4],
    influenceMin: [i === 0 ? -4 : i === 1 ? -1.5 : -0.75, -2, -5],
    influenceMax: [i === 0 ? 0.75 : i === 1 ? 1.5 : 4, 4, 5],
  }))
}
export async function showReflectionProbe(canvas, shaderDirectory = '/generated/', options = {}) {
  return showRasterLaboratory(canvas, shaderDirectory, {
    ...options,
    createView: async (options) => {
      const view = await Nova.createReflectionProbeView(options)
      view.setReflectionProbes(roomProbes())
      return view
    },
    initialPlaying: true,
    initialScale: 0.75,
    showChecker: false,
    showSplit: false,
    panelLabel: 'Reflection probe parameters',
    describe: (view) =>
      `Probe · ${view.getReflectionProbe().boxProjection ? 'Box projection' : 'Uncorrected'} · ${view.getProbeCaptureCount()} captures · `,
    extraControls: (api) => {
      let selected = 0
      const { view, control, controller, changed } = api
      controller.getReflectionProbe = () => view.getReflectionProbe()
      controller.setReflectionProbe = (value) => {
        view.setReflectionProbe(value)
        changed()
      }
      controller.updateProbe = () => {
        view.updateProbe(selected)
        changed()
      }
      const set = (key, value) => controller.setReflectionProbe({ [key]: value })
      control(
        'Local cubemap',
        'Local cubemap',
        'checkbox',
        () => view.getReflectionProbe().enabled,
        (v) => set('enabled', v),
      )
      control(
        'Box projection',
        'Box projection',
        'checkbox',
        () => view.getReflectionProbe().boxProjection,
        (v) => set('boxProjection', v),
      )
      for (const [key, label, aria, max, step] of [
        ['roughness', 'Roughness', 'Reflection roughness', 1, 0.01],
        ['intensity', 'Reflection intensity', 'Reflection intensity', 2, 0.05],
      ])
        control(
          label,
          aria,
          'range',
          () => view.getReflectionProbe()[key],
          (v) => set(key, v),
          [0, max, step],
        )
      const volume = () => view.getReflectionProbes()[selected]
      const setVolume = (patch) => {
        const probes = view.getReflectionProbes()
        probes[selected] = { ...probes[selected], ...patch }
        view.setReflectionProbes(probes)
        changed()
      }
      control(
        'Active probe',
        'Active probe',
        'select',
        () => String(selected),
        (v) => {
          selected = Number(v)
          set('debugProbe', selected)
        },
        [
          ['0', 'Red'],
          ['1', 'Green'],
          ['2', 'Blue'],
        ],
      )
      control(
        'Smooth blending',
        'Smooth blending',
        'checkbox',
        () => view.getReflectionProbe().smoothBlending,
        (v) => set('smoothBlending', v),
      )
      control(
        'Probe enabled',
        'Active probe enabled',
        'checkbox',
        () => volume().enabled,
        (v) => setVolume({ enabled: v }),
      )
      for (const [key, label, aria] of [
        ['blendDistance', 'Boundary blend', 'Boundary blend distance'],
        ['intensity', 'Probe intensity', 'Active probe intensity'],
      ])
        control(
          label,
          aria,
          'range',
          () => volume()[key],
          (v) => setVolume({ [key]: v }),
          [0, 2, 0.05],
        )
      control(
        'Travel phase',
        'Travel phase',
        'range',
        () => controller.getTime(),
        (v) => {
          controller.setPlaying(false)
          controller.setTime(v)
        },
        [0, 6.28, 0.01],
      )
      for (const [index, axis, min, max] of [
        [0, 'X', -3, 3],
        [1, 'Y', -0.5, 2.5],
        [2, 'Z', -3, 3],
      ])
        control(
          'Probe ' + axis,
          'Probe position ' + axis,
          'range',
          () => volume().position[index],
          (v) => {
            const position = volume().position
            position[index] = v
            setVolume({ position })
          },
          [min, max, 0.1],
        )
      control(
        'Output',
        'Probe output',
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
        'Refresh probe',
        'button',
        () => 'Refresh probe',
        () => controller.updateProbe(),
      )
      let environment = 'builtin',
        loading = false
      control(
        'Environment',
        'Reflection environment',
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
