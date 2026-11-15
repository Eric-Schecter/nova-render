import { showRasterLaboratory } from './RasterLaboratory.js'
// The shared example host owns controls, orbit interaction and performance measurement.
export async function showAntiAliasing(canvas, shaderDirectory = '/generated/', options = {}) {
  return showRasterLaboratory(canvas, shaderDirectory, options)
}
