<script setup>
import { ref, onMounted, onBeforeUnmount } from 'vue'
import { withBase } from 'vitepress'
import HlslEditor from './HlslEditor.vue'
import { showHelmet } from '../../../web/examples/helmet.js'
import { showLighting } from '../../../web/examples/lighting.js'
import lightingJS from '../../../web/examples/lighting.js?raw'
import lightingCPP from '../../../examples/lighting/main.cpp?raw'
import helmetJS from '../../../web/examples/helmet.js?raw'
import helmetCPP from '../../../examples/helmet/main.cpp?raw'
import hlslSource from '../../../render/shaders/model.hlsl?raw'
const props=defineProps({lighting:Boolean})
const jsSource=props.lighting?lightingJS:helmetJS
const cppSource=props.lighting?lightingCPP:helmetCPP
const showExample=props.lighting?showLighting:showHelmet
const tab=ref('example'),canvas=ref(null),message=ref('加载 Damaged Helmet…'),ready=ref(false),visible=ref(true)
const yaw=ref(20),pitch=ref(5),distance=ref(3.6),exposure=ref(1)
let renderer,disposed=false,drawing=false,pending=false,observer
const camera=()=>({yaw:Number(yaw.value),pitch:Number(pitch.value),distance:Number(distance.value),exposure:Number(exposure.value)})
async function draw(){
  if(!renderer||disposed)return
  pending=true;if(drawing)return;drawing=true
  try {while(pending&&!disposed){pending=false;renderer.setCamera(camera());await renderer.drawFrame()}}
  catch(e){if(!disposed)message.value=e.message}
  finally{drawing=false}
}
function reset(){yaw.value=20;pitch.value=5;distance.value=3.6;exposure.value=1;draw()}
async function toggle(){if(visible.value){visible.value=false;return}await draw();if(!disposed)visible.value=true}
onMounted(async()=>{
  try {
    const next=await showExample(canvas.value,withBase('/generated/DamagedHelmet.glb'),withBase('/generated/'),camera(),{
      onChange(value){
        if(disposed)return
        yaw.value=value.yaw;pitch.value=value.pitch;distance.value=value.distance;exposure.value=value.exposure
      },
      onError(error){if(!disposed)message.value=error.message},
    })
    if(disposed){next.dispose();return}
    renderer=next;ready.value=true;message.value='模型已加载 · 15,452 个三角形 · '+(props.lighting?'PBR 光照':'基础颜色')
    observer=new ResizeObserver(()=>draw());observer.observe(canvas.value)
  }catch(e){if(!disposed)message.value=e.message}
})
onBeforeUnmount(()=>{disposed=true;observer?.disconnect();renderer?.dispose()})
</script>

<template>
<div class="model-example">
  <div class="toolbar"><strong>{{lighting?'PBR 光照':'Damaged Helmet'}}</strong><div><button :disabled="!ready" @click="reset">重置相机</button><button :disabled="!ready" :aria-expanded="visible" aria-controls="helmet-preview" @click="toggle">{{visible?'隐藏预览':'显示预览'}}</button></div></div>
  <pre role="status" aria-live="polite">{{message}}</pre>
  <div class="source"><div role="tablist" aria-label="模型示例代码"><button role="tab" :aria-selected="tab==='cpp'" @click="tab='cpp'">C++ 示例</button><button role="tab" :aria-selected="tab==='example'" @click="tab='example'">JavaScript 示例</button><button role="tab" :aria-selected="tab==='shader'" @click="tab='shader'">HLSL shader</button></div>
    <HlslEditor v-if="tab==='cpp'" :model-value="cppSource" language="cpp" readonly />
    <HlslEditor v-else-if="tab==='example'" :model-value="jsSource" language="javascript" readonly />
    <HlslEditor v-else :model-value="hlslSource" readonly />
  </div>
  <div class="camera">
    <label>水平角 {{Number(yaw).toFixed(1)}}°<input v-model="yaw" aria-label="水平角" type="range" min="-180" max="180" step="0.1" :disabled="!ready" @input="draw" /></label>
    <label>俯仰角 {{Number(pitch).toFixed(1)}}°<input v-model="pitch" aria-label="俯仰角" type="range" min="-85" max="85" step="0.1" :disabled="!ready" @input="draw" /></label>
    <label>距离 {{Number(distance).toFixed(2)}}<input v-model="distance" aria-label="距离" type="range" min="1.5" max="100" step="0.01" :disabled="!ready" @input="draw" /></label>
    <label v-if="lighting">曝光 {{exposure}}<input v-model="exposure" aria-label="曝光" type="range" min="0.1" max="3" step="0.1" :disabled="!ready" @input="draw" /></label>
  </div>
  <div id="helmet-preview" class="preview" :class="{collapsed:!visible}" :aria-hidden="!visible" :inert="!visible"><span class="hint">拖动旋转 · 滚轮缩放</span><canvas ref="canvas" width="800" height="650" aria-label="Damaged Helmet 模型预览" /></div>
  <p v-if="tab==='cpp'">C++ 示例在本地编译运行；网页预览执行 JavaScript 示例，同样使用封装的模型和相机 API。</p>
</div>
</template>

<style scoped>
.hint{display:block;font-size:12px;color:var(--vp-c-text-2);padding-bottom:6px}
.model-example{border:1px solid var(--vp-c-divider);border-radius:12px;overflow:hidden;overflow-anchor:none;margin:24px 0}.toolbar{display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px;background:var(--vp-c-bg-soft);padding:12px 16px}.toolbar>div,[role="tablist"]{display:flex;gap:8px;flex-wrap:wrap}button{padding:4px 12px;border:1px solid var(--vp-c-divider);border-radius:6px;color:var(--vp-c-brand-1)}button[aria-selected="true"]{background:var(--vp-c-brand-soft)}button:disabled,input:disabled{opacity:.5}.source{padding:16px;display:flex;flex-direction:column;gap:12px}pre{margin:0;padding:10px 16px;white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px;border-bottom:1px solid var(--vp-c-divider)}.camera{display:grid;grid-template-columns:1fr 1fr;gap:12px 20px;padding:12px 16px;border-top:1px solid var(--vp-c-divider);font-size:13px}.camera label{display:flex;flex-direction:column;gap:4px}.camera input{width:100%;accent-color:var(--vp-c-brand-1)}.preview{overflow:hidden;padding:0 16px 16px}.preview.collapsed{height:0;visibility:hidden;padding-bottom:0}canvas{display:block;width:100%;height:auto;aspect-ratio:800/650;border-radius:6px;background:#080b10}p{padding:0 16px;font-size:13px}@media(max-width:480px){.camera{grid-template-columns:1fr}}
</style>
