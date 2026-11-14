<script setup>
import { ref, onMounted, onBeforeUnmount } from 'vue'
import { withBase } from 'vitepress'
import HlslEditor from './HlslEditor.vue'
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
  lighting: Boolean,
  environment: Boolean,
  animation: Boolean,
  comparison: Boolean,
  weekend: Boolean,
})
const jsSource = props.weekend
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
const cppSource = props.weekend
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
const showExample = props.weekend
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
  message = ref('加载模型…'),
  ready = ref(false),
  visible = ref(true)
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
        props.weekend
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
        comparison
          ? '积分器对比'
          : animation
            ? '骨骼动画'
            : environment
              ? '天空盒与环境光'
              : lighting
                ? 'PBR 光照'
                : '模型加载'
      }}</strong>
      <div>
        <button
          :disabled="!ready"
          :aria-expanded="visible"
          aria-controls="helmet-preview"
          @click="toggle"
        >
          {{ visible ? '隐藏预览' : '显示预览' }}
        </button>
      </div>
    </div>
    <pre v-if="message" role="status" aria-live="polite">{{ message }}</pre>
    <div class="source">
      <div role="tablist" aria-label="模型示例代码">
        <button role="tab" :aria-selected="tab === 'cpp'" @click="tab = 'cpp'">C++ 示例</button
        ><button role="tab" :aria-selected="tab === 'example'" @click="tab = 'example'">
          JavaScript 示例</button
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
      <HlslEditor v-else :model-value="props.comparison ? rayHlslSource : hlslSource" readonly />
    </div>
    <div
      id="helmet-preview"
      class="preview"
      :class="{ collapsed: !visible }"
      :aria-hidden="!visible"
      :inert="!visible"
    >
      <span class="hint">拖动旋转 · 滚轮缩放</span
      ><canvas
        :class="{ comparison: props.comparison }"
        :style="props.comparison ? { aspectRatio: '1 / 1' } : {}"
        ref="canvas"
        width="800"
        height="650"
        :aria-label="animation ? '骨骼动画预览' : 'Damaged Helmet 模型预览'"
      />
    </div>
  </div>
</template>

<style scoped>
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
