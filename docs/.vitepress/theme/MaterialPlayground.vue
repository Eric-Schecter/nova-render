<script setup>
import { ref, onMounted, onBeforeUnmount } from 'vue'
import { withBase } from 'vitepress'
import HlslEditor from './HlslEditor.vue'
import { showMaterials } from '../../../web/examples/materials.js'
import source from '../../../web/examples/materials.js?raw'
const canvas = ref(null),
  ready = ref(false),
  error = ref(''),
  show = ref(false),
  slot = ref(0),
  color = ref('#ff3027'),
  roughness = ref(0.65),
  metallic = ref(0),
  checker = ref(false)
let example,
  observer,
  disposed = false,
  pending = false,
  running = false
const linear = (x) => (x <= 0.04045 ? x / 12.92 : ((x + 0.055) / 1.055) ** 2.4)
const display = (x) => (x <= 0.0031308 ? 12.92 * x : 1.055 * x ** (1 / 2.4) - 0.055)
function select() {
  const m = example.renderer.getMaterials()[Number(slot.value)]
  color.value =
    '#' +
    m.baseColor
      .slice(0, 3)
      .map((x) =>
        Math.round(display(x) * 255)
          .toString(16)
          .padStart(2, '0'),
      )
      .join('')
  roughness.value = m.roughness
  metallic.value = m.metallic
  checker.value = !!m.baseColorTexture
}
async function update() {
  pending = true
  if (running || !example) return
  running = true
  try {
    while (pending && !disposed) {
      pending = false
      const rgb = [1, 3, 5].map((i) => linear(parseInt(color.value.slice(i, i + 2), 16) / 255))
      await example.update(Number(slot.value), {
        baseColor: [...rgb, 1],
        roughness: Number(roughness.value),
        metallic: Number(metallic.value),
        baseColorTexture: checker.value ? example.checker : null,
      })
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
    const e = await showMaterials(canvas.value, withBase('/generated/'))
    if (disposed) {
      e.dispose()
      return
    }
    example = e
    select()
    ready.value = true
    observer = new ResizeObserver(() => {
      if (!running) example.renderer.drawFrame().catch((e) => (error.value = e.message))
    })
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
</script>
<template>
  <div class="material-example" :data-ready="ready">
    <HlslEditor :model-value="source" language="javascript" readonly />
    <p v-if="error" role="alert">{{ error }}</p>
    <div class="material-output">
      <canvas ref="canvas" aria-label="Material slots preview" />
      <div class="material-panel">
        <button :disabled="!ready" :aria-expanded="show" @click="show = !show">
          Material controls
        </button>
        <div v-show="show" class="material-controls">
          <label
            >Material slot<select v-model="slot" aria-label="Material slot" @change="select">
              <option :value="0">0 · Left</option>
              <option :value="1">1 · Right</option>
            </select></label
          >
          <label
            >Base color<input v-model="color" aria-label="Base color" type="color" @input="update"
          /></label>
          <label
            >Roughness · {{ Number(roughness).toFixed(2)
            }}<input
              v-model="roughness"
              aria-label="Roughness"
              type="range"
              min="0"
              max="1"
              step=".01"
              @input="update"
          /></label>
          <label
            >Metallic · {{ Number(metallic).toFixed(2)
            }}<input
              v-model="metallic"
              aria-label="Metallic"
              type="range"
              min="0"
              max="1"
              step=".01"
              @input="update"
          /></label>
          <label
            ><input
              v-model="checker"
              aria-label="Checker texture"
              type="checkbox"
              @change="update"
            />
            Checker texture</label
          >
        </div>
      </div>
    </div>
  </div>
</template>
<style scoped>
.material-output {
  position: relative;
}
.material-output canvas {
  display: block;
  width: 100%;
  aspect-ratio: 4/3;
  background: #000;
  border-radius: 8px;
}
.material-panel {
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
.material-panel button {
  padding: 6px 12px;
  cursor: pointer;
}
.material-controls {
  padding: 10px;
  width: 220px;
}
.material-controls label {
  display: block;
  margin-bottom: 8px;
}
.material-controls input:not([type='checkbox']),
.material-controls select {
  display: block;
  width: 100%;
  border: 1px solid var(--vp-c-divider);
  border-radius: 3px;
}
</style>
