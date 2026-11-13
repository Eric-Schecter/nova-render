import * as Nova from '../src/Nova.js'

// shader: 预编译资源，或本地服务可编译的 { source: HLSL }。
export async function showTriangle(canvas, shader, background = [0.025, 0.035, 0.055, 1]) {
  const renderer = await Nova.createRenderer({ canvas })
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

// 页面退出或替换示例时：renderer.dispose()
