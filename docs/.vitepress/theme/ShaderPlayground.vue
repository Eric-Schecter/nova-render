<script setup>
import { ref, onMounted, onBeforeUnmount } from 'vue'
import { withBase } from 'vitepress'
import HlslEditor from './HlslEditor.vue'
import { showTriangle } from '../../../web/examples/triangle.js'
import exampleCode from '../../../web/examples/triangle.js?raw'
const source=ref(''), message=ref('初始化 Nova Web…'), busy=ref(false), ready=ref(false), canvas=ref(null)
const tab=ref('example'), background=ref('dark')
const local=import.meta.env.DEV
let renderer, initial, disposed=false
async function run(reset=false) {
  if(busy.value||!initial)return
  busy.value=true;message.value='执行 Nova 示例…'
  try {
    if(reset){source.value=initial.source;background.value='dark'}
    renderer?.dispose();renderer=undefined
    const shader=source.value===initial.source ? initial : {source:source.value}
    const color=background.value==='light' ? [0.8,0.85,0.95,1] : [0.025,0.035,0.055,1]
    const next=await showTriangle(canvas.value,shader,color)
    if(disposed){next.dispose();return}
    renderer=next;ready.value=true;message.value='绘制成功 · 已执行 triangle.js 的 Nova API 调用'
  }catch(e){message.value=e.message}finally{busy.value=false}
}
onMounted(async()=>{
  try {
    const response=await fetch(withBase('/generated/triangle.json'));if(!response.ok)throw Error('缺少 shader，请先运行 npm run shaders:webgpu')
    initial=await response.json();source.value=initial.source
    if(!disposed)await run()
  }catch(e){message.value=e.message}
})
onBeforeUnmount(()=>{disposed=true;renderer?.dispose()})
</script>

<template>
  <div class="playground">
    <div class="toolbar"><strong>Nova 三角形示例</strong><span><button :disabled="busy||!initial" @click="run(false)">{{ busy ? '执行中…' : '运行' }}</button><button :disabled="busy||!initial" @click="run(true)">重置</button></span></div>
    <div class="panes"><div class="source-pane">
      <div role="tablist" aria-label="示例代码"><button role="tab" :aria-selected="tab==='example'" @click="tab='example'">JavaScript 示例</button><button role="tab" :aria-selected="tab==='shader'" @click="tab='shader'">HLSL shader</button></div>
      <HlslEditor v-if="tab==='example'" :model-value="exampleCode" language="javascript" readonly />
      <HlslEditor v-else v-model="source" :readonly="!local" />
    </div><div class="output"><label>背景色 <select v-model="background" :disabled="busy" @change="run(false)"><option value="dark">深色</option><option value="light">浅色</option></select></label><canvas ref="canvas" width="640" height="480" aria-label="WebGPU 三角形预览" /></div></div>
    <pre role="status" aria-live="polite">{{message}}</pre>
    <p>左侧 JavaScript 是右侧实际执行的示例源码。可切换背景色体验 setClearColor；HLSL 标签支持本地编辑。</p>
    <p v-if="!local">静态网站使用预编译 shader；修改 HLSL 需要本地开发服务。</p>
  </div>
</template>

<style scoped>
.playground{border:1px solid var(--vp-c-divider);border-radius:12px;overflow:hidden;margin:24px 0}.toolbar{display:flex;justify-content:space-between;align-items:center;padding:12px 16px;background:var(--vp-c-bg-soft)}button{border:1px solid var(--vp-c-divider);border-radius:6px;padding:4px 14px;margin-left:8px;color:var(--vp-c-brand-1)}select{border:1px solid var(--vp-c-divider);border-radius:5px;padding:2px 8px;background:var(--vp-c-bg)}button[aria-selected="true"]{background:var(--vp-c-brand-soft)}button:disabled{opacity:.45;cursor:not-allowed}.panes{display:grid;grid-template-columns:1fr 1fr}.panes .source-pane,.output{display:flex;flex-direction:column;min-width:0;padding:12px;gap:8px;font-size:12px}.panes .source-pane{border-right:1px solid var(--vp-c-divider)}canvas{width:100%;height:auto;aspect-ratio:4/3;background:#111827;border-radius:6px}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px;padding:12px 16px;margin:0;border-top:1px solid var(--vp-c-divider)}p{padding:0 16px;font-size:13px}@media(max-width:760px){.panes{grid-template-columns:1fr}.panes .source-pane{border-right:0;border-bottom:1px solid var(--vp-c-divider)}}
</style>
