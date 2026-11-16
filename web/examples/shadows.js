import * as Nova from '../src/Nova.js'
import { showRasterLaboratory } from './RasterLaboratory.js'

export async function showShadows(canvas, shaderDirectory = '/generated/', options = {}) {
  return showRasterLaboratory(canvas, shaderDirectory, {
    ...options,
    createView: Nova.createShadowView,
    initialPlaying: false,
    showChecker: false,
    panelLabel: 'Shadow parameters',
    describe: (view) => {
      const sh = view.getShadowSettings()
      return `${{ off: 'Off', hard: 'Hard Shadow', pcf: 'PCF', pcss: 'PCSS' }[sh.method]} · ${sh.resolution}² map · `
    },
    extraControls: ({ view, control, controller, changed }) => {
      controller.getShadowSettings = () => view.getShadowSettings()
      controller.setShadowSettings = (value) => {
        view.setShadowSettings(value)
        changed()
      }
      const set = (key, value) => controller.setShadowSettings({ [key]: value })
      control(
        'Shadow',
        'Shadow method',
        'select',
        () => view.getShadowSettings().method,
        (v) => set('method', v),
        [
          ['off', 'Off'],
          ['hard', 'Hard Shadow'],
          ['pcf', 'PCF'],
          ['pcss', 'PCSS'],
        ],
      )
      control(
        'Shadow map',
        'Shadow resolution',
        'select',
        () => view.getShadowSettings().resolution,
        (v) => set('resolution', Number(v)),
        [
          ['512', '512×512'],
          ['1024', '1024×1024'],
          ['2048', '2048×2048'],
        ],
      )
      for (const [key, label, aria, min, max, step] of [
        ['depthBias', 'Depth bias', 'Depth bias', 0, 0.01, 0.0001],
        ['normalBias', 'Normal bias', 'Normal bias', 0, 0.1, 0.001],
        ['filterRadius', 'PCF radius', 'PCF radius', 1, 6, 0.1],
        ['lightSize', 'PCSS light size', 'Light size', 0.01, 1.5, 0.01],
      ])
        control(
          label,
          aria,
          'range',
          () => view.getShadowSettings()[key],
          (v) => set(key, v),
          [min, max, step],
          () =>
            key === 'filterRadius'
              ? view.getShadowSettings().method === 'pcf'
              : key === 'lightSize'
                ? view.getShadowSettings().method === 'pcss'
                : true,
        )
      const angles = () => {
        const d = view.getShadowSettings().lightDirection
        return {
          azimuth: (Math.atan2(d[0], d[2]) * 180) / Math.PI,
          elevation: (Math.atan2(d[1], Math.hypot(d[0], d[2])) * 180) / Math.PI,
        }
      }
      for (const [key, label, aria, min, max] of [
        ['azimuth', 'Light azimuth', 'Light azimuth', -180, 180],
        ['elevation', 'Light elevation', 'Light elevation', 10, 80],
      ]) {
        control(
          label,
          aria,
          'range',
          () => angles()[key],
          (v) => {
            const a = { ...angles(), [key]: v },
              y = (a.azimuth * Math.PI) / 180,
              p = (a.elevation * Math.PI) / 180
            set('lightDirection', [
              Math.sin(y) * Math.cos(p),
              Math.sin(p),
              Math.cos(y) * Math.cos(p),
            ])
          },
          [min, max, 1],
        )
      }
    },
  })
}
