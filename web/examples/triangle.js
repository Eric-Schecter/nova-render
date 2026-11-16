import { instrumentRenderer } from './PerformancePanel.js'
import * as Nova from '../src/Nova.js'

// shader: Precompiled shader, or { source: HLSL } compiled by the local service.
export async function showTriangle(
  canvas,
  shader,
  background = [0.025, 0.035, 0.055, 1],
  shaderDirectory = '/generated/',
) {
  const renderer = instrumentRenderer(
    await Nova.createRenderer({ canvas, shaderDirectory }),
    canvas,
  )
  try {
    await renderer.setShader(shader)
    renderer.setClearColor(background)
    await renderer.drawFrame()
    renderer.enableParameterPanel()
    return renderer
  } catch (error) {
    renderer.dispose()
    throw error
  }
}

// On teardown or when replacing the example: renderer.dispose()
