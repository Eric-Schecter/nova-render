<script setup>
import { computed, onMounted, onBeforeUnmount, ref } from 'vue'
import { useData } from 'vitepress'
import { createHighlighterCore } from 'shiki/core'
import { createOnigurumaEngine } from 'shiki/engine/oniguruma'
import hlsl from 'shiki/langs/hlsl.mjs'
import light from 'shiki/themes/github-light.mjs'
import dark from 'shiki/themes/github-dark.mjs'
const props=defineProps({modelValue:{type:String,default:''},readonly:Boolean})
const emit=defineEmits(['update:modelValue'])
const { isDark }=useData()
const layer=ref(null), loaded=ref(false)
let highlighter, disposed=false
const html=computed(()=>loaded.value ? highlighter.codeToHtml(props.modelValue+'\n',{lang:'hlsl',theme:isDark.value?'github-dark':'github-light'}) : '')
onMounted(async()=>{
  try {
    const instance=await createHighlighterCore({langs:[hlsl],themes:[light,dark],engine:createOnigurumaEngine(import('shiki/wasm'))})
    if(disposed){instance.dispose();return}
    highlighter=instance;loaded.value=true
  } catch { /* Keep the plain editor usable if highlighting cannot initialize. */ }
})
onBeforeUnmount(()=>{disposed=true;highlighter?.dispose()})
function sync(event){if(layer.value){layer.value.scrollTop=event.target.scrollTop;layer.value.scrollLeft=event.target.scrollLeft}}
function tab(event){
  if(props.readonly)return
  event.preventDefault()
  const input=event.target
  input.setRangeText('    ',input.selectionStart,input.selectionEnd,'end')
  emit('update:modelValue',input.value)
}
</script>

<template>
  <div class="hlsl-editor" :class="{highlighted:loaded}">
    <div ref="layer" class="code-layer" aria-hidden="true" v-html="html" />
    <textarea :value="modelValue" :readonly="readonly" aria-label="HLSL shader 编辑器" wrap="off" spellcheck="false" autocapitalize="off" autocomplete="off" autocorrect="off" @input="emit('update:modelValue',$event.target.value)" @scroll="sync" @keydown.tab="tab" />
  </div>
</template>

<style scoped>
.hlsl-editor{position:relative;height:380px;min-height:180px;resize:vertical;overflow:hidden;border:1px solid var(--vp-c-divider);border-radius:6px;background:var(--vp-c-bg);font:12px/20px var(--vp-font-family-mono)}
.code-layer,textarea{position:absolute;inset:0;width:100%;height:100%;margin:0;padding:12px;border:0;font:inherit;letter-spacing:normal;tab-size:4;white-space:pre;box-sizing:border-box}
.code-layer{overflow:hidden;pointer-events:none}
.code-layer :deep(pre),.code-layer :deep(code){margin:0!important;padding:0!important;border:0!important;border-radius:0!important;background:transparent!important;font:inherit!important;letter-spacing:inherit!important;white-space:pre!important;overflow:visible!important}
textarea{resize:none;background:transparent;color:var(--vp-c-text-1);outline:none;overflow:auto}
.highlighted textarea{color:transparent;-webkit-text-fill-color:transparent;caret-color:var(--vp-c-text-1)}
textarea::selection{background:rgba(80,130,240,.3)}
.hlsl-editor:focus-within{border-color:var(--vp-c-brand-1);box-shadow:0 0 0 1px var(--vp-c-brand-1)}
</style>
