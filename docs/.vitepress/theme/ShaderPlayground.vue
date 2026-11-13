<script setup>
import { ref, onMounted, onBeforeUnmount } from 'vue'
import { withBase } from 'vitepress'
import HlslEditor from './HlslEditor.vue'
const source=ref(''), message=ref('初始化 WebGPU…'), busy=ref(false), ready=ref(false), canvas=ref(null)
const local=import.meta.env.DEV
let device, context, initial, disposed=false
async function draw(data) {
  device.pushErrorScope('validation')
  let failure
  try {
    const vertex=device.createShaderModule({code:data.vert}), fragment=device.createShaderModule({code:data.frag})
    for(const module of [vertex,fragment]) {
      const info=await module.getCompilationInfo()
      const errors=info.messages.filter(m=>m.type==='error')
      if(errors.length)throw Error(errors.map(m=>`${m.lineNum}:${m.linePos} ${m.message}`).join('\n'))
    }
    const format=navigator.gpu.getPreferredCanvasFormat()
    const pipeline=await device.createRenderPipelineAsync({layout:'auto',vertex:{module:vertex,entryPoint:'VSMain'},fragment:{module:fragment,entryPoint:'PSMain',targets:[{format}]},primitive:{topology:'triangle-list'}})
    if(disposed)return
    context.configure({device,format,alphaMode:'opaque'})
    const encoder=device.createCommandEncoder()
    const pass=encoder.beginRenderPass({colorAttachments:[{view:context.getCurrentTexture().createView(),clearValue:{r:0.025,g:0.035,b:0.055,a:1},loadOp:'clear',storeOp:'store'}]})
    pass.setPipeline(pipeline);pass.draw(3);pass.end();device.queue.submit([encoder.finish()])
    await device.queue.onSubmittedWorkDone()
  }catch(e){failure=e}finally{const error=await device.popErrorScope();if(error&&!failure)failure=Error(error.message)}
  if(failure)throw failure
}
async function run(reset=false) {
  if(!ready.value||busy.value)return
  busy.value=true;message.value='编译并绘制…'
  try {
    if(reset)source.value=initial.source
    let data=initial
    if(local) {
      const response=await fetch('/__nova/compile',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({source:source.value})})
      data=await response.json();if(!response.ok)throw Error(data.error)
    }
    await draw(data);message.value='绘制成功 · HLSL → DXC → SPIR-V → Naga → WGSL'
  }catch(e){message.value=e.message}finally{busy.value=false}
}
onMounted(async()=>{
  try {
    const response=await fetch(withBase('/generated/triangle.json'));if(!response.ok)throw Error('缺少生成的 shader，请先运行 npm run shaders:webgpu')
    initial=await response.json();source.value=initial.source
    if(!navigator.gpu)throw Error('此浏览器未启用 WebGPU。请使用支持的浏览器及 HTTPS / localhost。')
    const adapter=await navigator.gpu.requestAdapter();if(!adapter)throw Error('没有可用的 WebGPU 适配器')
    device=await adapter.requestDevice();context=canvas.value.getContext('webgpu');if(!context)throw Error('无法创建 WebGPU canvas')
    device.lost.then(info=>{if(!disposed){ready.value=false;message.value='GPU 设备丢失：'+info.message}})
    await draw(initial);ready.value=true;message.value='绘制成功 · 可修改位置或颜色后点击运行'
  }catch(e){message.value=e.message}
})
onBeforeUnmount(()=>{disposed=true;context?.unconfigure();device?.destroy()})
</script>

<template>
  <div class="playground">
    <div class="toolbar"><strong>HLSL 三角形</strong><span><button :disabled="!ready||busy||!local" @click="run(false)">{{ busy ? '编译中…' : '运行' }}</button><button :disabled="!ready||busy||!local" @click="run(true)">重置</button></span></div>
    <div class="panes"><div class="source-pane"><span>triangle.hlsl</span><HlslEditor v-model="source" :readonly="!local" /></div><div class="output"><span>WebGPU 预览</span><canvas ref="canvas" width="640" height="480" aria-label="WebGPU 三角形预览" /></div></div>
    <pre role="status" aria-live="polite">{{message}}</pre>
    <p v-if="!local">静态网站展示构建时生成的 shader。编辑 HLSL 并重新编译需要本地开发服务。</p>
  </div>
</template>

<style scoped>
.playground{border:1px solid var(--vp-c-divider);border-radius:12px;overflow:hidden;margin:24px 0}.toolbar{display:flex;justify-content:space-between;align-items:center;padding:12px 16px;background:var(--vp-c-bg-soft)}button{border:1px solid var(--vp-c-divider);border-radius:6px;padding:4px 14px;margin-left:8px;color:var(--vp-c-brand-1)}button:disabled{opacity:.45;cursor:not-allowed}.panes{display:grid;grid-template-columns:1fr 1fr}.panes .source-pane,.output{display:flex;flex-direction:column;min-width:0;padding:12px;gap:8px;font-size:12px}.panes .source-pane{border-right:1px solid var(--vp-c-divider)}canvas{width:100%;height:auto;aspect-ratio:4/3;background:#111827;border-radius:6px}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px;padding:12px 16px;margin:0;border-top:1px solid var(--vp-c-divider)}p{padding:0 16px;font-size:13px}@media(max-width:760px){.panes{grid-template-columns:1fr}.panes .source-pane{border-right:0;border-bottom:1px solid var(--vp-c-divider)}}
</style>
