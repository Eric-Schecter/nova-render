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
    panelLabel: '反射探针参数',
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
        '活动探针',
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
        '平滑混合',
        'checkbox',
        () => view.getReflectionProbe().smoothBlending,
        (v) => set('smoothBlending', v),
      )
      control(
        'Probe enabled',
        '活动探针开关',
        'checkbox',
        () => volume().enabled,
        (v) => setVolume({ enabled: v }),
      )
      for (const [key, label, aria] of [
        ['blendDistance', 'Boundary blend', '边界混合距离'],
        ['intensity', 'Probe intensity', '活动探针强度'],
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
        '移动相位',
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
          '探针位置 ' + axis,
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
