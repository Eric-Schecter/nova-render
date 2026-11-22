import { addPostProcessingControls } from './PostProcessingControls.js'
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
    panelLabel: 'Post-processing parameters',
    describe: (view) => {
      const p = view.getPostProcessing()
      return `${p.enabled ? { linear: 'Linear', reinhard: 'Reinhard', aces: 'ACES fit', agx: 'AgX' }[p.toneMapping] : 'Bypass'} · ${p.autoExposure ? 'Auto exposure' : 'Manual exposure'} · `
    },
    extraControls: (api) => {
      addPostProcessingControls(api)
      const { view, control, controller, changed } = api
      controller.getSceneIntensity = () => view.getSceneIntensity()
      controller.setSceneIntensity = (v) => {
        view.setSceneIntensity(v)
        changed()
      }
      control(
        'Scene intensity',
        'Scene intensity',
        'range',
        () => view.getSceneIntensity(),
        (v) => controller.setSceneIntensity(v),
        [0.05, 20, 0.05],
      )
    },
  })
}
