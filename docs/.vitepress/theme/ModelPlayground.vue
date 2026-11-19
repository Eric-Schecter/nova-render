<script setup>
import { ref, onMounted, onBeforeUnmount } from 'vue'
import { withBase } from 'vitepress'
import { showShowroom } from '../../../web/examples/showroom.js'
import showroomJS from '../../../web/examples/showroom.js?raw'
import showroomCPP from '../../../examples/showroom/main.cpp?raw'
import HlslEditor from './HlslEditor.vue'
import { showHybrid } from '../../../web/examples/hybrid.js'
import hybridJS from '../../../web/examples/hybrid.js?raw'
import hybridCPP from '../../../examples/hybrid/main.cpp?raw'
import { showDeferred } from '../../../web/examples/deferred.js'
import deferredJS from '../../../web/examples/deferred.js?raw'
import deferredCPP from '../../../examples/deferred/main.cpp?raw'
import { showAO } from '../../../web/examples/ao.js'
import aoJS from '../../../web/examples/ao.js?raw'
import aoCPP from '../../../examples/ao/main.cpp?raw'
import aoHLSL from '../../../render/shaders/ambient-occlusion.hlsl?raw'
import { showWeekend } from '../../../web/examples/weekend.js'
import weekendJS from '../../../web/examples/weekend.js?raw'
import { showComparison } from '../../../web/examples/comparison.js'
import comparisonJS from '../../../web/examples/comparison.js?raw'
import comparisonCPP from '../../../examples/comparison/main.cpp?raw'
import rayHlslSource from '../../../render/shaders/pathtrace.hlsl?raw'
import { showAnimation } from '../../../web/examples/animation.js'
import animationJS from '../../../web/examples/animation.js?raw'
import animationCPP from '../../../examples/animation/main.cpp?raw'
import { showHelmet } from '../../../web/examples/helmet.js'
import { showEnvironment } from '../../../web/examples/environment.js'
import environmentJS from '../../../web/examples/environment.js?raw'
import environmentCPP from '../../../examples/environment/main.cpp?raw'
import { showLighting } from '../../../web/examples/lighting.js'
import lightingJS from '../../../web/examples/lighting.js?raw'
import lightingCPP from '../../../examples/lighting/main.cpp?raw'
import helmetJS from '../../../web/examples/helmet.js?raw'
import helmetCPP from '../../../examples/helmet/main.cpp?raw'
import hlslSource from '../../../render/shaders/model.hlsl?raw'
const props = defineProps({
  showroom: Boolean,
  hybrid: Boolean,
  deferred: Boolean,
  lighting: Boolean,
  environment: Boolean,
  animation: Boolean,
  comparison: Boolean,
  weekend: Boolean,
  ao: Boolean,
})
const jsSource = props.showroom
  ? showroomJS
  : props.hybrid
    ? hybridJS
    : props.deferred
      ? deferredJS
      : props.ao
        ? aoJS
        : props.weekend
          ? weekendJS
          : props.comparison
            ? comparisonJS
            : props.animation
              ? animationJS
              : props.environment
                ? environmentJS
                : props.lighting
                  ? lightingJS
                  : helmetJS
const cppSource = props.showroom
  ? showroomCPP
  : props.hybrid
    ? hybridCPP
    : props.deferred
      ? deferredCPP
      : props.ao
        ? aoCPP
        : props.weekend
          ? environmentCPP
          : props.comparison
            ? comparisonCPP
            : props.animation
              ? animationCPP
              : props.environment
                ? environmentCPP
                : props.lighting
                  ? lightingCPP
                  : helmetCPP
const showExample = props.showroom
  ? showShowroom
  : props.hybrid
    ? showHybrid
    : props.deferred
      ? showDeferred
      : props.ao
        ? showAO
        : props.weekend
          ? showWeekend
          : props.comparison
            ? showComparison
            : props.animation
              ? showAnimation
              : props.environment
                ? showEnvironment
                : props.lighting
                  ? showLighting
                  : showHelmet
const tab = ref('example'),
  canvas = ref(null),
  message = ref('Loading model…'),
  ready = ref(false),
  visible = ref(true),
  hybridScenes = ref([]),
  hybridViews = ref([]),
  hybridScene = ref('reflections-test'),
  hybridView = ref('angle-1'),
  sceneLoading = ref(false)
let hybridControls
async function changeHybridScene() {
  sceneLoading.value = true
  try {
    await hybridControls.selectScene(hybridScene.value)
    hybridViews.value = hybridControls.views
    hybridView.value = hybridControls.view
    message.value = ''
  } catch (error) {
    hybridScene.value = hybridControls.scene
    message.value = error.message
  } finally {
    sceneLoading.value = false
  }
}
async function changeHybridView() {
  sceneLoading.value = true
  try {
    await hybridControls.selectView(hybridView.value)
    message.value = ''
  } catch (error) {
    message.value = error.message
  } finally {
    sceneLoading.value = false
  }
}
let renderer,
  disposed = false,
  drawing = false,
  pending = false,
  observer
async function draw() {
  if (!renderer || disposed) return
  pending = true
  if (drawing) return
  drawing = true
  try {
    while (pending && !disposed) {
      pending = false
      await renderer.drawFrame()
    }
  } catch (e) {
    if (!disposed) message.value = e.message
  } finally {
    drawing = false
  }
}
async function toggle() {
  if (visible.value) {
    visible.value = false
    return
  }
  await draw()
  if (!disposed) visible.value = true
}
onMounted(async () => {
  try {
    const next = await showExample(
      canvas.value,
      withBase(
        props.ao
          ? '/generated/SponzaWhite.glb'
          : props.weekend
            ? '/generated/WeekendSpheres.glb'
            : props.comparison
              ? '/generated/CornellCaustics.glb'
              : props.animation
                ? '/generated/Fox.glb'
                : '/generated/DamagedHelmet.glb',
      ),
      withBase('/generated/'),
      {
        yaw: props.weekend ? 25 : props.comparison ? 0 : props.animation ? 65 : 20,
        pitch: props.weekend ? 12 : props.comparison ? 0 : props.animation ? 10 : 5,
        distance: props.weekend ? 2.8 : props.animation ? 3 : 3.6,
        exposure: props.comparison ? 3 : 1,
      },
      {
        onHybridControls(controls) {
          hybridControls = controls
          hybridScenes.value = controls.choices
          hybridViews.value = controls.views
          hybridScene.value = controls.scene
          hybridView.value = controls.view
        },
        environmentURL: withBase('/generated/venice_sunset.bin'),
        onError(error) {
          if (!disposed) message.value = error.message
        },
      },
    )
    if (disposed) {
      next.dispose()
      return
    }
    renderer = next
    canvas.value.addEventListener('nova-error', (event) => {
      if (!disposed) message.value = event.detail.message
    })
    ready.value = true
    message.value = ''
    observer = new ResizeObserver(() => draw())
    observer.observe(canvas.value)
  } catch (e) {
    if (!disposed) message.value = e.message
  }
})
onBeforeUnmount(() => {
  disposed = true
  observer?.disconnect()
  renderer?.dispose()
})
</script>

<template>
  <div class="model-example" :data-ready="ready">
    <div class="toolbar">
      <strong>{{
        showroom
          ? 'CarConcept showroom'
          : hybrid
            ? 'Hybrid rendering'
            : deferred
              ? 'Deferred model rendering'
              : ao
                ? 'Ambient occlusion'
                : comparison
                  ? 'Integrator comparison'
                  : animation
                    ? 'Skeletal animation'
                    : environment
                      ? 'Skybox and environment lighting'
                      : lighting
                        ? 'PBR lighting'
                        : 'Model loading'
      }}</strong>
      <div>
        <button
          :disabled="!ready"
          :aria-expanded="visible"
          aria-controls="helmet-preview"
          @click="toggle"
        >
          {{ visible ? 'Hide preview' : 'Show preview' }}
        </button>
      </div>
    </div>
    <pre v-if="message" role="status" aria-live="polite">{{ message }}</pre>
    <div class="source">
      <div role="tablist" aria-label="Model example source">
        <button role="tab" :aria-selected="tab === 'cpp'" @click="tab = 'cpp'">C++ example</button
        ><button role="tab" :aria-selected="tab === 'example'" @click="tab = 'example'">
          JavaScript example</button
        ><button role="tab" :aria-selected="tab === 'shader'" @click="tab = 'shader'">
          HLSL shader
        </button>
      </div>

      <HlslEditor v-if="tab === 'cpp'" :model-value="cppSource" language="cpp" readonly />
      <HlslEditor
        v-else-if="tab === 'example'"
        :model-value="jsSource"
        language="javascript"
        readonly
      />
      <HlslEditor
        v-else
        :model-value="props.ao ? aoHLSL : props.comparison ? rayHlslSource : hlslSource"
        readonly
      />
    </div>
    <div
      id="helmet-preview"
      class="preview"
      :class="{ collapsed: !visible }"
      :aria-hidden="!visible"
      :inert="!visible"
    >
      <div v-if="hybrid && ready" class="scene-controls" :aria-busy="sceneLoading">
        <label
          >Scene
          <select
            v-model="hybridScene"
            aria-label="Hybrid scene"
            :disabled="sceneLoading"
            @change="changeHybridScene"
          >
            <option v-for="scene in hybridScenes" :key="scene.id" :value="scene.id">
              {{ scene.name }}
            </option>
          </select></label
        >
        <label
          >Camera preset
          <select
            v-model="hybridView"
            aria-label="Hybrid camera preset"
            :disabled="sceneLoading"
            @change="changeHybridView"
          >
            <option v-for="view in hybridViews" :key="view.id" :value="view.id">
              {{ view.name }}
            </option>
          </select></label
        >
        <button :disabled="sceneLoading" @click="changeHybridView">Reset view</button>
      </div>
      <span class="hint">Drag to orbit · Scroll to zoom</span>
      <div v-if="comparison" ref="canvas" class="comparison-host" />
      <canvas
        v-else
        ref="canvas"
        width="800"
        :height="showroom ? 505 : hybrid ? 450 : 650"
        :class="{ hybrid, showroom }"
        :aria-label="
          showroom
            ? 'CarConcept showroom preview'
            : hybrid
              ? 'Hybrid rendering preview'
              : animation
                ? 'Skeletal animation preview'
                : 'Damaged Helmet preview'
        "
      />
    </div>
  </div>
</template>

<style scoped>
.scene-controls {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
  align-items: end;
  margin-bottom: 12px;
}
.scene-controls label {
  display: flex;
  flex-direction: column;
  gap: 4px;
  font-size: 12px;
}
.scene-controls select,
.scene-controls button {
  border: 1px solid var(--vp-c-divider);
  border-radius: 5px;
  padding: 5px 8px;
  background: var(--vp-c-bg-soft);
  color: var(--vp-c-text-1);
}
.scene-controls :disabled {
  opacity: 0.6;
}
canvas.showroom {
  aspect-ratio: 750/473;
}
canvas.hybrid {
  aspect-ratio: 16/9;
}
.hint {
  display: block;
  font-size: 12px;
  color: var(--vp-c-text-2);
  padding-bottom: 6px;
}
.model-example {
  border: 1px solid var(--vp-c-divider);
  border-radius: 12px;
  overflow: hidden;
  overflow-anchor: none;
  margin: 24px 0;
}
.toolbar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  flex-wrap: wrap;
  gap: 10px;
  background: var(--vp-c-bg-soft);
  padding: 12px 16px;
}
.toolbar > div,
[role='tablist'] {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
}
button {
  padding: 4px 12px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 6px;
  color: var(--vp-c-brand-1);
}
button[aria-selected='true'] {
  background: var(--vp-c-brand-soft);
}
button:disabled,
input:disabled {
  opacity: 0.5;
}
.source {
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}
pre {
  margin: 0;
  padding: 10px 16px;
  white-space: pre-wrap;
  overflow-wrap: anywhere;
  font-size: 12px;
  border-bottom: 1px solid var(--vp-c-divider);
}
.preview {
  overflow: hidden;
  padding: 0 16px 16px;
}
.preview.collapsed {
  height: 0;
  visibility: hidden;
  padding-bottom: 0;
}
canvas {
  display: block;
  width: 100%;
  height: auto;
  aspect-ratio: 800/650;
  border-radius: 6px;
  background: #080b10;
}
p {
  padding: 0 16px;
  font-size: 13px;
}
</style>
