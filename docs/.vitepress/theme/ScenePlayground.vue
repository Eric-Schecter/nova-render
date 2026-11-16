<script setup>
import { ref, onMounted, onBeforeUnmount } from 'vue'
import { withBase } from 'vitepress'
import HlslEditor from './HlslEditor.vue'
import { showScene } from '../../../web/examples/scene.js'
import js from '../../../web/examples/scene.js?raw'
import cpp from '../../../examples/scene/main.cpp?raw'
const canvas = ref(null),
  ready = ref(false),
  error = ref(''),
  source = ref('js'),
  visible = ref(true),
  panelVisible = ref(false)
let renderer,
  disposed = false
function toggleParameters() {
  panelVisible.value = !panelVisible.value
  if (panelVisible.value) renderer.enableParameterPanel()
  else renderer.disableParameterPanel()
}
onMounted(async () => {
  try {
    const next = await showScene(canvas.value, withBase('/generated/'))
    if (disposed) {
      next.dispose()
      return
    }
    renderer = next
    ready.value = true
  } catch (e) {
    if (!disposed) error.value = e.message
  }
})
onBeforeUnmount(() => {
  disposed = true
  renderer?.dispose()
})
</script>

<template>
  <div class="scene-example" :data-ready="ready">
    <div class="scene-toolbar">
      <div role="tablist" aria-label="Example source">
        <button role="tab" :aria-selected="source === 'cpp'" @click="source = 'cpp'">
          C++ example
        </button>
        <button role="tab" :aria-selected="source === 'js'" @click="source = 'js'">
          JavaScript example
        </button>
      </div>
      <button :disabled="!ready" :aria-pressed="panelVisible" @click="toggleParameters">
        Parameters
      </button>
      <button @click="visible = !visible">{{ visible ? 'Hide preview' : 'Show preview' }}</button>
    </div>
    <HlslEditor
      :model-value="source === 'cpp' ? cpp : js"
      :language="source === 'cpp' ? 'cpp' : 'javascript'"
      readonly
    />
    <p v-if="error" role="alert">{{ error }}</p>
    <div v-show="visible" class="scene-viewport">
      <canvas ref="canvas" aria-label="Scene instances preview" />
    </div>
  </div>
</template>

<style scoped>
.scene-example {
  border: 1px solid var(--vp-c-divider);
  border-radius: 12px;
  padding: 16px;
}
.scene-toolbar {
  display: flex;
  flex-wrap: wrap;
  justify-content: space-between;
  gap: 8px;
}
button {
  border: 1px solid var(--vp-c-divider);
  border-radius: 5px;
  padding: 4px 10px;
  cursor: pointer;
}
button[aria-selected='true'] {
  color: var(--vp-c-brand-1);
  background: var(--vp-c-brand-soft);
}
pre {
  max-height: 380px;
  overflow: auto;
  font-size: 12px;
  line-height: 1.6;
}
.scene-viewport {
  position: relative;
}
canvas {
  display: block;
  width: 100%;
  aspect-ratio: 4/3;
  border-radius: 6px;
}
</style>
