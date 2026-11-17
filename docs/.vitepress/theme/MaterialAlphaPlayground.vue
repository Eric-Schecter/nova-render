<script setup>
import { ref, onMounted, onBeforeUnmount } from 'vue'
import { withBase } from 'vitepress'
import { showMaterialAlpha } from '../../../web/examples/material-alpha.js'
const canvas = ref(null),
  ready = ref(false),
  error = ref(''),
  show = ref(false),
  fixture = ref('Layers'),
  slot = ref(0),
  materials = ref([]),
  mode = ref('OPAQUE'),
  opacity = ref(1),
  cutoff = ref(0.5),
  doubleSided = ref(false),
  lighting = ref('unlit'),
  pipeline = ref('forward')
let example,
  observer,
  disposed = false,
  running = false,
  pending = false
function select() {
  const m = materials.value[slot.value]
  if (!m) return
  mode.value = m.alphaMode
  opacity.value = m.baseColor[3]
  cutoff.value = m.alphaCutoff
  doubleSided.value = m.doubleSided
}
function refresh() {
  materials.value = example.renderer.getMaterials()
  select()
}
async function update() {
  pending = true
  if (running || !example) return
  running = true
  try {
    while (pending && !disposed) {
      pending = false
      const m = example.renderer.getMaterials()[slot.value]
      await example.update(Number(slot.value), {
        alphaMode: mode.value,
        baseColor: [...m.baseColor.slice(0, 3), Number(opacity.value)],
        alphaCutoff: Number(cutoff.value),
        doubleSided: doubleSided.value,
      })
      materials.value = example.renderer.getMaterials()
    }
    error.value = ''
  } catch (e) {
    error.value = e.message
  } finally {
    running = false
  }
}
async function load() {
  if (running) return
  running = true
  try {
    await example.load(fixture.value)
    slot.value = 0
    refresh()
    error.value = ''
  } catch (e) {
    error.value = e.message
  } finally {
    running = false
  }
}
async function settings() {
  try {
    example.renderer.setLighting(lighting.value)
    example.renderer.setRasterSettings({ pipeline: pipeline.value })
    await example.renderer.drawFrame()
  } catch (e) {
    error.value = e.message
  }
}
onMounted(async () => {
  try {
    const e = await showMaterialAlpha(canvas.value, withBase('/generated/'))
    if (disposed) {
      e.dispose()
      return
    }
    example = e
    refresh()
    ready.value = true
    observer = new ResizeObserver(() => {
      if (!running) example.renderer.drawFrame().catch((e) => (error.value = e.message))
    })
    observer.observe(canvas.value)
  } catch (e) {
    error.value = e.message
  }
})
onBeforeUnmount(() => {
  disposed = true
  observer?.disconnect()
  example?.dispose()
})
</script>
<template>
  <div class="alpha-example" :data-ready="ready">
    <p v-if="error" role="alert">{{ error }}</p>
    <div class="alpha-output">
      <canvas ref="canvas" aria-label="Alpha material validation preview" />
      <div class="alpha-panel">
        <button :disabled="!ready" :aria-expanded="show" @click="show = !show">
          Material controls
        </button>
        <div v-show="show" class="alpha-controls">
          <label
            >Fixture<select
              v-model="fixture"
              aria-label="Fixture"
              :disabled="running"
              @change="load"
            >
              <option>Layers</option>
              <option>LayersReverse</option>
              <option>Mask</option>
              <option>Quad</option>
              <option>Back</option>
              <option>Mirrored</option>
            </select></label
          >
          <label
            >Material slot<select
              v-model="slot"
              aria-label="Material slot"
              :disabled="running"
              @change="select"
            >
              <option v-for="(m, i) in materials" :key="i" :value="i">
                {{ i }} · {{ m.name }}
              </option>
            </select></label
          >
          <label
            >Alpha mode<select v-model="mode" aria-label="Alpha mode" @change="update">
              <option>OPAQUE</option>
              <option>MASK</option>
              <option>BLEND</option>
            </select></label
          >
          <label
            >Opacity · {{ Number(opacity).toFixed(2)
            }}<input
              v-model="opacity"
              aria-label="Opacity"
              type="range"
              min="0"
              max="1"
              step=".01"
              @input="update"
          /></label>
          <label
            >Alpha cutoff · {{ Number(cutoff).toFixed(2)
            }}<input
              v-model="cutoff"
              aria-label="Alpha cutoff"
              type="range"
              min="0"
              max="1"
              step=".01"
              :disabled="mode !== 'MASK'"
              @input="update"
          /></label>
          <label
            ><input
              v-model="doubleSided"
              aria-label="Double sided"
              type="checkbox"
              @change="update"
            />Double sided</label
          >
          <label
            >Lighting<select v-model="lighting" aria-label="Lighting" @change="settings">
              <option value="unlit">Unlit</option>
              <option value="pbr">PBR</option>
            </select></label
          >
          <label
            >Raster pipeline<select
              v-model="pipeline"
              aria-label="Raster pipeline"
              @change="settings"
            >
              <option value="forward">Forward</option>
              <option value="deferred">Deferred</option>
            </select></label
          >
        </div>
      </div>
    </div>
  </div>
</template>
<style scoped>
.alpha-output {
  position: relative;
}
.alpha-output canvas {
  display: block;
  width: 100%;
  aspect-ratio: 4/3;
  background: #000;
  border-radius: 8px;
}
.alpha-panel {
  position: absolute;
  right: 12px;
  top: 12px;
  max-width: 230px;
  max-height: calc(100% - 24px);
  overflow: auto;
  border: 1px solid var(--vp-c-divider);
  border-radius: 8px;
  background: var(--vp-c-bg-soft);
  padding: 10px;
  font-size: 13px;
}
.alpha-panel button {
  font-weight: 600;
}
.alpha-controls {
  display: grid;
  gap: 8px;
  padding-top: 10px;
}
.alpha-controls label {
  display: grid;
  gap: 2px;
}
.alpha-controls select {
  border: 1px solid var(--vp-c-divider);
  border-radius: 4px;
  padding: 2px 5px;
}
.alpha-controls input[type='checkbox'] {
  justify-self: start;
}
</style>
