import assert from 'node:assert/strict'
import { createFrameMeter } from '../../web/examples/FrameMeter.js'
let now = 20
const meter = createFrameMeter(() => now)
meter.record(0, { width: 100, height: 80, scale: 0.5, samples: 16, method: 'hbao' })
assert.equal(meter.snapshot('rendering').fps, null)
now = 1020
meter.record(1000, { width: 100, height: 80, scale: 0.5, samples: 16, method: 'hbao' })
assert.equal(
  meter.snapshot('rendering').fps,
  1,
  'FPS must measure completion cadence, not 1000/drawMs',
)
assert.equal(meter.snapshot('rendering').lastRenderMs, 20)
assert.equal(meter.snapshot('idle').fps, null, 'On-demand idle must not be shown as a low FPS')
assert.equal(meter.snapshot('paused').fps, null)
now = 1100
meter.record(1020, { width: 100, height: 80, scale: 0.5, samples: 16, method: 'hbao' })
assert.equal(meter.snapshot('rendering').p95Ms, 80)
console.log('PASS performance cadence, idle/pause and percentile semantics')
