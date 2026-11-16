<script setup>
import { ref, onMounted, onBeforeUnmount, nextTick } from 'vue'
import { withBase } from 'vitepress'
import HlslEditor from './HlslEditor.vue'
import { showScreenSpaceReflections } from '../../../web/examples/ssr.js'
import jsCode from '../../../web/examples/ssr.js?raw'
import cppCode from '../../../examples/ssr/main.cpp?raw'
import shaderCode from '../../../render/shaders/ssr.hlsl?raw'
const canvas = ref(null),
  ready = ref(false),
  message = ref('Initializing…'),
  tab = ref('example'),
  preview = ref(true),
  busy = ref(false)
let scene,
  disposed = false
const report = (e) => {
  message.value = e.message
}
async function run() {
  if (busy.value) return
  busy.value = true
  ready.value = false
  message.value = ''
  try {
    scene?.dispose()
    scene = await showScreenSpaceReflections(canvas.value, withBase('/generated/'), {
      onError: report,
    })
    if (disposed) {
      scene.dispose()
      return
    }
    ready.value = true
  } catch (e) {
    report(e)
  } finally {
    busy.value = false
  }
}
async function toggle() {
  preview.value = !preview.value
  await nextTick()
  if (preview.value) scene?.drawFrame().catch(report)
}
onMounted(run)
onBeforeUnmount(() => {
  disposed = true
  scene?.dispose()
})
</script>
<template>
  <div class="aa-playground" :data-ready="ready && !busy">
    <div class="aa-toolbar">
      <strong>Nova screen-space reflections</strong>
      <div class="aa-actions">
        <button :disabled="busy" @click="run">Reset</button
        ><button
          :disabled="busy"
          :aria-expanded="preview"
          aria-controls="nova-ssr-preview"
          @click="toggle"
        >
          {{ preview ? 'Hide preview' : 'Show preview' }}
        </button>
      </div>
    </div>
    <pre v-if="message" role="status">{{ message }}</pre>
    <div class="aa-source">
      <div role="tablist" aria-label="Example source">
        <button role="tab" :aria-selected="tab === 'cpp'" @click="tab = 'cpp'">C++ example</button
        ><button role="tab" :aria-selected="tab === 'example'" @click="tab = 'example'">
          JavaScript example</button
        ><button role="tab" :aria-selected="tab === 'shader'" @click="tab = 'shader'">
          HLSL shader
        </button>
      </div>
      <HlslEditor v-if="tab === 'cpp'" :model-value="cppCode" language="cpp" readonly /><HlslEditor
        v-else-if="tab === 'example'"
        :model-value="jsCode"
        language="javascript"
        readonly
      /><HlslEditor v-else :model-value="shaderCode" readonly />
    </div>
    <div
      id="nova-ssr-preview"
      class="aa-preview"
      :class="{ collapsed: !preview }"
      :aria-hidden="!preview"
      :inert="!preview"
    >
      <canvas ref="canvas" width="800" height="485" aria-label="Screen-space reflection output" />
    </div>
  </div>
</template>
<style scoped>
.aa-playground {
  margin: 24px 0;
  border: 1px solid var(--vp-c-divider);
  border-radius: 12px;
  overflow: hidden;
  overflow-anchor: none;
}
.aa-toolbar {
  display: flex;
  flex-wrap: wrap;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
  padding: 12px 16px;
  background: var(--vp-c-bg-soft);
  font-size: 13px;
}
.aa-actions,
[role='tablist'] {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}
button {
  border: 1px solid var(--vp-c-divider);
  border-radius: 6px;
  padding: 4px 12px;
  color: var(--vp-c-brand-1);
  font-size: 12px;
}
button[aria-selected='true'] {
  background: var(--vp-c-brand-soft);
}
button:disabled {
  opacity: 0.45;
}
.aa-source {
  display: flex;
  flex-direction: column;
  gap: 12px;
  padding: 16px;
  min-width: 0;
}
.aa-preview {
  padding: 16px;
  border-top: 1px solid var(--vp-c-divider);
  overflow: hidden;
}
.aa-preview.collapsed {
  height: 0;
  visibility: hidden;
  padding-top: 0;
  padding-bottom: 0;
  border-top: 0;
}
canvas {
  display: block;
  width: 100%;
  aspect-ratio: 1.65;
}
pre[role='status'] {
  white-space: pre-wrap;
  padding: 12px;
  color: var(--vp-c-danger-1);
}
</style>
