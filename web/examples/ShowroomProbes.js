import { cameraBasis } from '../src/Camera.js'
import { hybridProbeGridDimensions as grid } from '../src/HybridProbes.js'

export function attachShowroomProbes(canvas, renderer, bounds) {
  const host = canvas.parentElement
  const stage = document.createElement('div')
  stage.style.position = 'relative'
  const overlay = document.createElement('canvas')
  overlay.setAttribute('aria-hidden', 'true')
  Object.assign(overlay.style, {
    display: 'none',
    position: 'absolute',
    inset: '0',
    width: '100%',
    height: '100%',
    pointerEvents: 'none',
  })
  canvas.before(stage)
  stage.append(canvas, overlay)
  const control = document.createElement('label')
  control.style.display = 'block'
  const checkbox = document.createElement('input')
  checkbox.type = 'checkbox'
  control.append(checkbox, ' Show DDGI probes')
  host.before(control)

  const colors = ['#6ae1ff', '#7bffbb', '#ffdf84', '#e9a1ff']
  const context = overlay.getContext('2d')
  function update() {
    if (!checkbox.checked || !overlay.isConnected) return
    const width = canvas.width,
      height = canvas.height
    if (overlay.width !== width) overlay.width = width
    if (overlay.height !== height) overlay.height = height
    context.clearRect(0, 0, width, height)
    const { c, right, up, forward } = cameraBasis(renderer.getViewCamera())
    const focal = height / (2 * Math.tan((c.verticalFov * Math.PI) / 360))
    const radius = 2.75 * (width / Math.max(canvas.clientWidth, 1))
    const dot = (a, b) => a.reduce((sum, v, i) => sum + v * b[i], 0)
    for (let y = 0; y < grid[1]; y++) {
      for (let z = 0; z < grid[2]; z++) {
        for (let x = 0; x < grid[0]; x++) {
          const offset = [x, y, z].map(
            (i, k) =>
              bounds.minimum[k] +
              ((i + 0.5) * (bounds.maximum[k] - bounds.minimum[k])) / grid[k] -
              c.position[k],
          )
          const depth = dot(offset, forward)
          if (depth < c.nearPlane || depth > c.farPlane) continue
          context.beginPath()
          context.arc(
            width / 2 + (focal * dot(offset, right)) / depth,
            height / 2 - (focal * dot(offset, up)) / depth,
            radius,
            0,
            2 * Math.PI,
          )
          context.strokeStyle = '#0c1118'
          context.lineWidth = radius * 0.8
          context.stroke()
          context.fillStyle = colors[y]
          context.fill()
        }
      }
    }
  }
  checkbox.addEventListener('change', () => {
    overlay.style.display = checkbox.checked ? 'block' : 'none'
    update()
  })
  return {
    update,
    dispose() {
      control.remove()
      stage.before(canvas)
      stage.remove()
    },
  }
}
