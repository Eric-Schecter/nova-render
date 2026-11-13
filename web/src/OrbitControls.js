// DOM and pointer handling stay inside Nova; examples only enable the controller.
export function attachOrbitControls(canvas, { getCamera, setCamera, drawFrame, onChange, onError, rotateSpeed = 0.35, zoomSpeed = 0.001 }) {
  if (!Number.isFinite(rotateSpeed) || rotateSpeed <= 0 || !Number.isFinite(zoomSpeed) || zoomSpeed <= 0)
    throw Error('Orbit control speeds must be positive finite numbers')
  if ((onChange != null && typeof onChange !== 'function') || (onError != null && typeof onError !== 'function'))
    throw Error('Orbit callbacks must be functions')
  const previous = { cursor: canvas.style.cursor, touchAction: canvas.style.touchAction }
  let disposed = false, pointer, x = 0, y = 0, frame = 0, rendering = false, pending = false
  const clamp = (value, min, max) => Math.min(max, Math.max(min, value))
  const report = error => {
    if (disposed) return
    try { if (onError) onError(error); else console.error('[Nova orbit controls]', error) }
    catch (callbackError) { console.error('[Nova orbit controls callback]', callbackError) }
  }
  function requestDraw() {
    if (disposed) return
    pending = true
    if (!frame && !rendering) frame = requestAnimationFrame(flush)
  }
  async function flush() {
    frame = 0
    if (disposed) return
    pending = false; rendering = true
    try { await drawFrame() } catch (error) { report(error) }
    finally { rendering = false; if (pending && !disposed) requestDraw() }
  }
  function update(camera) {
    setCamera(camera)
    onChange?.({ ...getCamera() })
    requestDraw()
  }
  function down(event) {
    if (disposed || pointer !== undefined || !event.isPrimary || event.button !== 0) return
    pointer = event.pointerId; x = event.clientX; y = event.clientY
    try { canvas.setPointerCapture(pointer) } catch { pointer = undefined; return }
    canvas.style.cursor = 'grabbing'; event.preventDefault()
  }
  function move(event) {
    if (disposed || event.pointerId !== pointer) return
    if (event.pointerType === 'mouse' && !(event.buttons & 1)) { up(event); return }
    const dx = event.clientX - x, dy = event.clientY - y
    x = event.clientX; y = event.clientY
    if (![dx, dy].every(Number.isFinite)) return
    try {
      const camera = getCamera()
      camera.yaw = ((camera.yaw - clamp(dx, -10000, 10000) * rotateSpeed + 180) % 360 + 360) % 360 - 180
      camera.pitch = clamp(camera.pitch + clamp(dy, -10000, 10000) * rotateSpeed, -85, 85)
      update(camera)
    } catch (error) { report(error) }
  }
  function up(event) {
    if (event.pointerId !== pointer) return
    const id = pointer; pointer = undefined
    if (canvas.hasPointerCapture(id)) canvas.releasePointerCapture(id)
    canvas.style.cursor = 'grab'
  }
  function wheel(event) {
    if (disposed || !Number.isFinite(event.deltaY) || !event.deltaY) return
    event.preventDefault()
    const unit = event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? canvas.clientHeight || 800 : 1
    try {
      const camera = getCamera()
      camera.distance = clamp(camera.distance * Math.exp(clamp(event.deltaY * unit, -1000, 1000) * zoomSpeed), 1.5, 100)
      update(camera)
    } catch (error) { report(error) }
  }
  canvas.style.cursor = 'grab'; canvas.style.touchAction = 'none'
  canvas.addEventListener('pointerdown', down)
  canvas.addEventListener('pointermove', move)
  canvas.addEventListener('pointerup', up)
  canvas.addEventListener('pointercancel', up)
  canvas.addEventListener('lostpointercapture', up)
  canvas.addEventListener('wheel', wheel, { passive: false })
  return {
    dispose() {
      if (disposed) return
      disposed = true
      cancelAnimationFrame(frame); pending = false
      if (pointer !== undefined) up({ pointerId: pointer })
      canvas.removeEventListener('pointerdown', down)
      canvas.removeEventListener('pointermove', move)
      canvas.removeEventListener('pointerup', up)
      canvas.removeEventListener('pointercancel', up)
      canvas.removeEventListener('lostpointercapture', up)
      canvas.removeEventListener('wheel', wheel)
      canvas.style.cursor = previous.cursor; canvas.style.touchAction = previous.touchAction
    },
  }
}
