<script setup>
import { ref, onMounted, onBeforeUnmount } from 'vue'
import { withBase } from 'vitepress'
import HlslEditor from './HlslEditor.vue'
import { showCamera } from '../../../web/examples/camera.js'
import source from '../../../web/examples/camera.js?raw'
const canvas = ref(null),
  ready = ref(false),
  error = ref(''),
  show = ref(false)
const projection = ref('perspective'),
  fov = ref(45),
  height = ref(4),
  distance = ref(6),
  roll = ref(0),
  near = ref(0.1),
  far = ref(100),
  inset = ref(false)
let renderer,
  observer,
  disposed = false,
  pending = false,
  running = false
async function update() {
  if (!renderer || disposed) return
  pending = true
  if (running) return
  running = true
  try {
    while (pending && !disposed) {
      pending = false
      const rad = (Number(roll.value) * Math.PI) / 360
      renderer.setViewCamera({
        projection: projection.value,
        position: [0, 0, Number(distance.value)],
        orientation: [0, 0, Math.sin(rad), Math.cos(rad)],
        verticalFov: Number(fov.value),
        orthographicHeight: Number(height.value),
        nearPlane: Number(near.value),
        farPlane: Number(far.value),
      })
      const w = Math.round(canvas.value.clientWidth * (window.devicePixelRatio || 1)),
        h = Math.round(canvas.value.clientHeight * (window.devicePixelRatio || 1))
      renderer.setViewport(
        inset.value
          ? {
              x: Math.floor(w * 0.15),
              y: Math.floor(h * 0.15),
              width: Math.max(1, Math.floor(w * 0.7)),
              height: Math.max(1, Math.floor(h * 0.7)),
            }
          : null,
      )
      await renderer.drawFrame()
      error.value = ''
    }
  } catch (e) {
    error.value = e.message
  } finally {
    running = false
  }
}
onMounted(async () => {
  try {
    const r = await showCamera(canvas.value, withBase('/generated/'))
    if (disposed) {
      r.dispose()
      return
    }
    renderer = r
    ready.value = true
    observer = new ResizeObserver(() => update())
    observer.observe(canvas.value)
  } catch (e) {
    if (!disposed) error.value = e.message
  }
})
onBeforeUnmount(() => {
  disposed = true
  observer?.disconnect()
  renderer?.dispose()
})
</script>
<template>
  <div class="camera-example" :data-ready="ready">
    <HlslEditor :model-value="source" language="javascript" readonly />
    <p v-if="error" role="alert">{{ error }}</p>
    <div class="camera-output">
      <canvas ref="canvas" aria-label="Camera and viewport preview" />
      <div class="camera-panel">
        <button :disabled="!ready" :aria-expanded="show" @click="show = !show">
          Camera controls
        </button>
        <div v-show="show" class="camera-controls">
          <label
            >Projection<select v-model="projection" aria-label="Projection" @change="update">
              <option value="perspective">Perspective</option>
              <option value="orthographic">Orthographic</option>
            </select></label
          >
          <label
            >Vertical FOV · {{ fov }}°<input
              v-model="fov"
              aria-label="Vertical FOV"
              type="range"
              min="15"
              max="120"
              @input="update"
          /></label>
          <label
            >Orthographic height · {{ height
            }}<input
              v-model="height"
              aria-label="Orthographic height"
              type="range"
              min="1"
              max="10"
              step=".1"
              @input="update"
          /></label>
          <label
            >Camera distance · {{ distance
            }}<input
              v-model="distance"
              aria-label="Camera distance"
              type="range"
              min="3"
              max="12"
              step=".1"
              @input="update"
          /></label>
          <label
            >Roll · {{ roll }}°<input
              v-model="roll"
              aria-label="Roll"
              type="range"
              min="-180"
              max="180"
              @input="update"
          /></label>
          <label
            >Near plane<input
              v-model="near"
              aria-label="Near plane"
              type="number"
              min=".01"
              step=".1"
              @change="update"
          /></label>
          <label
            >Far plane<input
              v-model="far"
              aria-label="Far plane"
              type="number"
              min=".02"
              step="1"
              @change="update"
          /></label>
          <label
            ><input v-model="inset" aria-label="Inset viewport" type="checkbox" @change="update" />
            Inset viewport</label
          >
        </div>
      </div>
    </div>
  </div>
</template>
<style scoped>
.camera-output {
  position: relative;
}
.camera-output canvas {
  display: block;
  width: 100%;
  aspect-ratio: 4/3;
  background: #000;
  border-radius: 8px;
}
.camera-panel {
  position: absolute;
  right: 12px;
  top: 52px;
  max-width: calc(100% - 24px);
  max-height: calc(100% - 64px);
  overflow: auto;
  background: var(--vp-c-bg);
  border: 1px solid var(--vp-c-divider);
  border-radius: 6px;
  font-size: 12px;
}
.camera-panel button {
  padding: 6px 12px;
  cursor: pointer;
}
.camera-controls {
  padding: 10px;
  width: 220px;
}
.camera-controls label {
  display: block;
  margin-bottom: 8px;
}
.camera-controls input:not([type='checkbox']),
.camera-controls select {
  display: block;
  width: 100%;
  border: 1px solid var(--vp-c-divider);
  border-radius: 3px;
}
</style>
