<script setup>
import { computed, onMounted, onBeforeUnmount, ref, nextTick, useId, watch } from 'vue'
import { useData } from 'vitepress'
import { createHighlighterCore } from 'shiki/core'
import { createOnigurumaEngine } from 'shiki/engine/oniguruma'
import hlsl from 'shiki/langs/hlsl.mjs'
import cpp from 'shiki/langs/cpp.mjs'
import javascript from 'shiki/langs/javascript.mjs'
import light from 'shiki/themes/github-light.mjs'
import dark from 'shiki/themes/github-dark.mjs'
const props = defineProps({
  modelValue: { type: String, default: '' },
  readonly: Boolean,
  expanded: Boolean,
  language: { type: String, default: 'hlsl' },
})
const emit = defineEmits(['update:modelValue', 'update:expanded'])
const { isDark } = useData()
const editorId = useId()
const input = ref(null)
const expandedHeight = computed(
  () => `${Math.max(180, (props.modelValue.split('\n').length + 1) * 20 + 24)}px`,
)
watch(
  () => props.expanded,
  async (expanded) => {
    if (!expanded) return
    await nextTick()
    if (input.value) input.value.scrollTop = 0
    if (layer.value) layer.value.scrollTop = 0
  },
)
const layer = ref(null),
  loaded = ref(false)
let highlighter,
  disposed = false
const html = computed(() =>
  loaded.value
    ? highlighter.codeToHtml(props.modelValue + '\n', {
        lang: props.language,
        theme: isDark.value ? 'github-dark' : 'github-light',
      })
    : '',
)
onMounted(async () => {
  try {
    const instance = await createHighlighterCore({
      langs: [hlsl, javascript, cpp],
      themes: [light, dark],
      engine: createOnigurumaEngine(import('shiki/wasm')),
    })
    if (disposed) {
      instance.dispose()
      return
    }
    highlighter = instance
    loaded.value = true
  } catch {
    /* Keep the plain editor usable if highlighting cannot initialize. */
  }
})
onBeforeUnmount(() => {
  disposed = true
  highlighter?.dispose()
})
function sync(event) {
  if (layer.value) {
    layer.value.scrollTop = event.target.scrollTop
    layer.value.scrollLeft = event.target.scrollLeft
  }
}
function tab(event) {
  if (props.readonly) return
  event.preventDefault()
  const input = event.target
  input.setRangeText('    ', input.selectionStart, input.selectionEnd, 'end')
  emit('update:modelValue', input.value)
}
</script>

<template>
  <div class="code-view">
    <div class="code-actions">
      <button
        type="button"
        :aria-expanded="expanded"
        :aria-controls="editorId"
        @click="emit('update:expanded', !expanded)"
      >
        {{ expanded ? '收起代码' : '展开代码' }}
      </button>
    </div>
    <div
      :id="editorId"
      class="hlsl-editor"
      :class="{ highlighted: loaded, expanded }"
      :style="expanded ? { height: expandedHeight } : undefined"
    >
      <div ref="layer" class="code-layer" aria-hidden="true" v-html="html" />
      <textarea
        ref="input"
        :value="modelValue"
        :readonly="readonly"
        :aria-label="
          language === 'hlsl'
            ? 'HLSL shader 编辑器'
            : language === 'cpp'
              ? 'C++ 示例源码'
              : 'JavaScript 示例源码'
        "
        wrap="off"
        spellcheck="false"
        autocapitalize="off"
        autocomplete="off"
        autocorrect="off"
        @input="emit('update:modelValue', $event.target.value)"
        @scroll="sync"
        @keydown.tab="tab"
      />
    </div>
  </div>
</template>

<style scoped>
.code-view {
  min-width: 0;
}
.code-actions {
  display: flex;
  justify-content: flex-end;
  margin-bottom: 8px;
}
.code-actions button {
  padding: 4px 12px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 6px;
  color: var(--vp-c-brand-1);
  font-size: 12px;
}
.code-actions button:focus-visible {
  outline: 2px solid var(--vp-c-brand-1);
  outline-offset: 2px;
}
.hlsl-editor.expanded {
  resize: none;
}
.hlsl-editor {
  position: relative;
  height: 380px;
  min-height: 180px;
  resize: vertical;
  overflow: hidden;
  border: 1px solid var(--vp-c-divider);
  border-radius: 6px;
  background: var(--vp-c-bg);
  font: 12px/20px var(--vp-font-family-mono);
}
.code-layer,
textarea {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  margin: 0;
  padding: 12px;
  border: 0;
  font: inherit;
  letter-spacing: normal;
  tab-size: 4;
  white-space: pre;
  box-sizing: border-box;
}
.code-layer {
  overflow: hidden;
  pointer-events: none;
}
.code-layer :deep(pre),
.code-layer :deep(code) {
  margin: 0 !important;
  padding: 0 !important;
  border: 0 !important;
  border-radius: 0 !important;
  background: transparent !important;
  font: inherit !important;
  letter-spacing: inherit !important;
  white-space: pre !important;
  overflow: visible !important;
}
textarea {
  resize: none;
  background: transparent;
  color: var(--vp-c-text-1);
  outline: none;
  overflow: auto;
}
.highlighted textarea {
  color: transparent;
  -webkit-text-fill-color: transparent;
  caret-color: var(--vp-c-text-1);
}
textarea::selection {
  background: rgba(80, 130, 240, 0.3);
}
.hlsl-editor:focus-within {
  border-color: var(--vp-c-brand-1);
  box-shadow: 0 0 0 1px var(--vp-c-brand-1);
}
</style>
