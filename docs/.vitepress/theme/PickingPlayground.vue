<script setup>
import { ref, onMounted, onBeforeUnmount } from 'vue'
import { withBase } from 'vitepress'
import HlslEditor from './HlslEditor.vue'
import { showPicking } from '../../../web/examples/picking.js'
import source from '../../../web/examples/picking.js?raw'
const canvas = ref(null),
  surface = ref(null),
  ready = ref(false),
  error = ref(''),
  hit = ref(null),
  show = ref(false),
  include = ref('all'),
  excludeFront = ref(false),
  marker = ref(null)
let example,
  observer,
  disposed = false,
  drawing = false,
  pending = false
function report(value) {
  hit.value = value
  if (value) {
    const half = example.renderer.getViewCamera().orthographicHeight * 0.5,
      aspect = canvas.value.width / canvas.value.height
    const c = canvas.value.getBoundingClientRect(),
      s = surface.value.getBoundingClientRect()
    marker.value = {
      left: c.left - s.left + ((value.position[0] / (half * aspect)) * 0.5 + 0.5) * c.width + 'px',
      top: c.top - s.top + (0.5 - (value.position[1] / half) * 0.5) * c.height + 'px',
    }
  } else marker.value = null
}
function filters() {
  example.setFilters(
    include.value === 'all' ? [] : [include.value],
    excludeFront.value ? ['front'] : [],
  )
  report(null)
}
function pick(x) {
  try {
    example.pickAt(x, 0)
    error.value = ''
  } catch (e) {
    error.value = e.message
  }
}
async function resize() {
  if (!example || disposed) return
  pending = true
  if (drawing) return
  drawing = true
  try {
    while (pending && !disposed) {
      pending = false
      await example.renderer.drawFrame()
      if (hit.value) report(hit.value)
    }
  } catch (e) {
    if (!disposed) error.value = e.message
  } finally {
    drawing = false
  }
}
onMounted(async () => {
  try {
    const next = await showPicking(canvas.value, withBase('/generated/'), report)
    if (disposed) {
      next.dispose()
      return
    }
    example = next
    ready.value = true
    observer = new ResizeObserver(resize)
    observer.observe(canvas.value)
  } catch (e) {
    if (!disposed) error.value = e.message
  }
})
onBeforeUnmount(() => {
  disposed = true
  observer?.disconnect()
  example?.dispose()
})
const vector = (v) => v.map((x) => x.toFixed(3)).join(', ')
</script>
<template>
  <div class="picking-example" :data-ready="ready">
    <HlslEditor :model-value="source" language="javascript" readonly />
    <p v-if="error" role="alert">{{ error }}</p>
    <div class="picking-output">
      <div ref="surface" class="picking-canvas">
        <canvas ref="canvas" aria-label="Scene picking preview" />
        <span v-if="marker" class="hit-marker" :style="marker" aria-hidden="true" />
      </div>
      <div class="picking-panel">
        <button :disabled="!ready" :aria-expanded="show" @click="show = !show">
          Picking controls
        </button>
        <div v-show="show" class="picking-controls">
          <label
            >Include instances<select
              v-model="include"
              aria-label="Include instances"
              @change="filters"
            >
              <option value="all">All</option>
              <option value="front">Front only</option>
              <option value="back">Back only</option>
              <option value="right">Right only</option>
            </select></label
          >
          <label
            ><input
              v-model="excludeFront"
              aria-label="Exclude front"
              type="checkbox"
              @change="filters"
            />
            Exclude front</label
          >
          <button @click="pick(-1)">Pick left</button><button @click="pick(1.2)">Pick right</button>
        </div>
      </div>
    </div>
    <dl class="hit-values" aria-label="Picking result" aria-live="polite">
      <div>
        <dt>Instance</dt>
        <dd data-hit-id>{{ hit?.id ?? '—' }}</dd>
      </div>
      <div>
        <dt>Position</dt>
        <dd>{{ hit ? vector(hit.position) : '—' }}</dd>
      </div>
      <div>
        <dt>Normal</dt>
        <dd data-hit-normal>{{ hit ? vector(hit.normal) : '—' }}</dd>
      </div>
      <div>
        <dt>Distance</dt>
        <dd data-hit-distance>{{ hit ? hit.distance.toFixed(3) : '—' }}</dd>
      </div>
    </dl>
  </div>
</template>
<style scoped>
.picking-output {
  position: relative;
}
.picking-canvas {
  position: relative;
}
.picking-canvas canvas {
  display: block;
  width: 100%;
  aspect-ratio: 4/3;
  cursor: crosshair;
  border-radius: 8px;
  background: #000;
}
.hit-marker {
  position: absolute;
  width: 12px;
  height: 12px;
  transform: translate(-50%, -50%);
  border: 2px solid white;
  border-radius: 50%;
  pointer-events: none;
  outline: 1px solid black;
}
.picking-panel {
  position: absolute;
  right: 12px;
  top: 52px;
  max-width: calc(100% - 24px);
  max-height: calc(100% - 64px);
  overflow: auto;
  border: 1px solid var(--vp-c-divider);
  border-radius: 6px;
  background: var(--vp-c-bg);
  font-size: 12px;
}
.picking-panel button {
  padding: 6px 10px;
  cursor: pointer;
}
.picking-controls {
  padding: 10px;
  width: 210px;
}
.picking-controls label {
  display: block;
  margin-bottom: 10px;
}
.picking-controls select {
  display: block;
  width: 100%;
  border: 1px solid var(--vp-c-divider);
  border-radius: 3px;
}
.hit-values {
  display: flex;
  flex-wrap: wrap;
  gap: 8px 24px;
  font-size: 12px;
}
.hit-values > div {
  min-width: 100px;
}
.hit-values dt {
  color: var(--vp-c-text-2);
}
.hit-values dd {
  margin: 0;
  font-family: var(--vp-font-family-mono);
}
</style>
