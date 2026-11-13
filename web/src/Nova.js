import { createModelView } from './ModelView.js'
import { validateCamera } from './ModelData.js'
// Browser adapter. Native C++ backends and the web adapter share concepts, not binaries.
export async function createRenderer({ canvas, shaderDirectory = '/generated/' }) {
  if (!navigator.gpu) throw Error('此浏览器未启用 WebGPU，请使用支持的浏览器及 HTTPS / localhost。')
  const adapter = await navigator.gpu.requestAdapter()
  if (!adapter) throw Error('没有可用的 WebGPU 适配器')
  const device = await adapter.requestDevice()
  const context = canvas.getContext('webgpu')
  if (!context) { device.destroy(); throw Error('无法创建 WebGPU canvas') }
  return new Renderer(canvas, device, context, shaderDirectory)
}
class Renderer {
  #canvas; #device; #context; #pipeline; #configured = false; #disposed = false; #lost; #clear = [0.025, 0.035, 0.055, 1]
  #model; #shaderDirectory; #camera = {yaw:0,pitch:0,distance:3.6,exposure:1}
  constructor(canvas, device, context, shaderDirectory) {
    this.#shaderDirectory=shaderDirectory;
    this.#canvas = canvas; this.#device = device; this.#context = context
    device.lost.then(info => { this.#lost = info.message || 'GPU device lost' })
  }
  #check() { if (this.#disposed) throw Error('Renderer 已释放'); if (this.#lost) throw Error(this.#lost) }
  async loadModel(url) {
    this.#check()
    this.#device.pushErrorScope('validation')
    let next, failure
    try { next=await createModelView(this.#device,this.#context,url,this.#shaderDirectory.replace(/\/?$/, '/')+'model.json') }
    catch(e){failure=e}
    const error=await this.#device.popErrorScope()
    if(failure||error){next?.dispose();throw failure||Error(error.message)}
    if(this.#disposed){next.dispose();throw Error('Renderer 已释放')}
    await this.waitIdle();this.#model?.dispose();this.#model=next
    return {triangles:next.triangles}
  }
  setCamera(camera) { this.#check();const value={...this.#camera,...camera};validateCamera(value);this.#camera=value }
  async setShader({ source, vert, frag }) {
    this.#check()
    // Generated packages carry source + WGSL. Edited HLSL is compiled locally.
    if (!vert || !frag) {
      const response = await fetch('/__nova/compile', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({source}) })
      const data = await response.json()
      if (!response.ok) throw Error(data.error || 'HLSL 编译失败')
      vert = data.vert; frag = data.frag
    }
    this.#check()
    const device = this.#device
    device.pushErrorScope('validation')
    let pipeline, failure
    try {
      const vertex = device.createShaderModule({code:vert}), fragment = device.createShaderModule({code:frag})
      for (const module of [vertex, fragment]) {
        const info = await module.getCompilationInfo()
        const errors = info.messages.filter(m => m.type === 'error')
        if (errors.length) throw Error(errors.map(m => `${m.lineNum}:${m.linePos} ${m.message}`).join('\n'))
      }
      pipeline = await device.createRenderPipelineAsync({layout:'auto',vertex:{module:vertex,entryPoint:'VSMain'},fragment:{module:fragment,entryPoint:'PSMain',targets:[{format:navigator.gpu.getPreferredCanvasFormat()}]},primitive:{topology:'triangle-list'}})
    } catch (e) { failure = e }
    const error = await device.popErrorScope()
    if (failure || error) throw failure || Error(error.message)
    this.#check(); this.#pipeline = pipeline
  }
  setClearColor(color) {
    this.#check()
    if (!Array.isArray(color) || color.length !== 4 || !color.every(v => Number.isFinite(v) && v >= 0 && v <= 1)) throw Error('背景色需要 4 个 0～1 的数值')
    this.#clear = [...color]
  }
  async drawFrame() {
    this.#check()
    if (!this.#pipeline && !this.#model) throw Error('请先调用 setShader 或 loadModel')
    const rect = this.#canvas.getBoundingClientRect()
    if (!rect.width || !rect.height) return 'skipped'
    const ratio = window.devicePixelRatio || 1
    const width = Math.min(this.#device.limits.maxTextureDimension2D, Math.max(1, Math.round(rect.width * ratio)))
    const height = Math.min(this.#device.limits.maxTextureDimension2D, Math.max(1, Math.round(rect.height * ratio)))
    if (this.#canvas.width !== width) this.#canvas.width = width
    if (this.#canvas.height !== height) this.#canvas.height = height
    if (!this.#configured) {
      this.#context.configure({device:this.#device,format:navigator.gpu.getPreferredCanvasFormat(),alphaMode:'opaque'})
      this.#configured = true
    }
    this.#device.pushErrorScope('validation')
    let failure
    try {
      if(this.#model) {
        this.#model.draw(this.#camera,this.#clear,width,height)
      } else {
      const encoder = this.#device.createCommandEncoder()
      const pass = encoder.beginRenderPass({colorAttachments:[{view:this.#context.getCurrentTexture().createView(),clearValue:this.#clear,loadOp:'clear',storeOp:'store'}]})
      pass.setPipeline(this.#pipeline); pass.draw(3); pass.end()
      this.#device.queue.submit([encoder.finish()]);
      }
      await this.waitIdle()
    } catch(e) { failure = e }
    const error = await this.#device.popErrorScope()
    if (failure || error) throw failure || Error(error.message)
    return 'rendered'
  }
  async waitIdle() { this.#check(); await this.#device.queue.onSubmittedWorkDone() }
  dispose() { if (this.#disposed) return; this.#disposed = true; this.#model?.dispose(); this.#context.unconfigure(); this.#device.destroy() }
}
