import * as Nova from '../src/Nova.js'
import { instrumentRenderer } from './PerformancePanel.js'

// This example uses a fixed, axis-aligned orthographic camera. The renderer's
// picking API itself accepts world-space rays and is independent of camera/UI.
export async function showPicking(canvas, shaderDirectory, onHit) {
  const renderer = instrumentRenderer(
    await Nova.createRenderer({ canvas, shaderDirectory }),
    canvas,
  )
  const events = new AbortController()
  let includeIds = [],
    excludeIds = []
  function pickAt(x, y) {
    const hit = renderer.pickScene({
      origin: [x, y, 6],
      direction: [0, 0, -1],
      includeIds,
      excludeIds,
    })
    onHit(hit)
    return hit
  }
  function pickScreen(event) {
    const rectangle = canvas.getBoundingClientRect(),
      halfHeight = renderer.getViewCamera().orthographicHeight * 0.5
    const x =
      ((((event.clientX - rectangle.left) / rectangle.width) * 2 - 1) * halfHeight * canvas.width) /
      canvas.height
    const y = (1 - ((event.clientY - rectangle.top) / rectangle.height) * 2) * halfHeight
    return pickAt(x, y)
  }
  try {
    const matrix = (x, z, scale = 1) => [scale, 0, 0, 0, 0, scale, 0, 0, 0, 0, scale, 0, x, 0, z, 1]
    const c = Math.cos(Math.PI / 6) * 0.7,
      s = Math.sin(Math.PI / 6) * 0.7
    await renderer.setScene(
      [
        { id: 'red', url: shaderDirectory + 'SceneRed.glb' },
        { id: 'blue', url: shaderDirectory + 'SceneBlue.glb' },
      ],
      [
        { id: 'back', assetId: 'blue', world: matrix(-1, -1.2) },
        { id: 'front', assetId: 'red', world: matrix(-1, 0.4, 0.7) },
        {
          id: 'right',
          assetId: 'blue',
          world: [-c, 0, s, 0, 0, 0.7, 0, 0, s, 0, c, 0, 1.2, 0, 0, 1],
        },
      ],
    )
    renderer.setViewCamera({
      position: [0, 0, 6],
      orientation: [0, 0, 0, 1],
      projection: 'orthographic',
      orthographicHeight: 4.5,
    })
    await renderer.drawFrame()
    canvas.addEventListener('click', pickScreen, { signal: events.signal })
    return {
      renderer,
      pickAt,
      setFilters(include, exclude) {
        includeIds = [...include]
        excludeIds = [...exclude]
      },
      dispose() {
        events.abort()
        renderer.dispose()
      },
    }
  } catch (error) {
    events.abort()
    renderer.dispose()
    throw error
  }
}
