import { loadGLB, sceneUniforms } from './ModelData.js'
export async function createModelView(device,context,url,shaderURL){
  const data=await loadGLB(url), resources=[]
  try{
    const response=await fetch(shaderURL);if(!response.ok)throw Error('Missing model shader package: '+shaderURL)
    const shader=await response.json()
    const makeBuffer=(bytes,usage)=>{const buffer=device.createBuffer({size:bytes.byteLength,usage:usage|GPUBufferUsage.COPY_DST});resources.push(buffer);device.queue.writeBuffer(buffer,0,bytes);return buffer}
    const vertices=makeBuffer(data.vertices,GPUBufferUsage.STORAGE)
    const uniform=makeBuffer(new Float32Array(40),GPUBufferUsage.UNIFORM)
    const textures=[]
    for(const image of data.images){
      const texture=device.createTexture({size:[image.width,image.height],format:'rgba8unorm',usage:GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_DST|GPUTextureUsage.RENDER_ATTACHMENT});resources.push(texture);textures.push(texture)
      if(image.bitmap)device.queue.copyExternalImageToTexture({source:image.bitmap,flipY:false},{texture},[image.width,image.height])
      else device.queue.writeTexture({texture},image.pixels,{bytesPerRow:image.width*4},[image.width,image.height])
    }
    const layout=device.createBindGroupLayout({entries:[
      {binding:0,visibility:GPUShaderStage.VERTEX|GPUShaderStage.FRAGMENT,buffer:{type:'uniform',minBindingSize:160}},
      {binding:1,visibility:GPUShaderStage.VERTEX,buffer:{type:'read-only-storage'}},
      ...textures.map((_,i)=>({binding:i+2,visibility:GPUShaderStage.FRAGMENT,texture:{sampleType:'float'}})),
      {binding:7,visibility:GPUShaderStage.FRAGMENT,sampler:{type:'filtering'}},
    ]})
    const sampler=device.createSampler({minFilter:'linear',magFilter:'linear',addressModeU:'repeat',addressModeV:'repeat'})
    const bindGroup=device.createBindGroup({layout,entries:[{binding:0,resource:{buffer:uniform}},{binding:1,resource:{buffer:vertices}},...textures.map((t,i)=>({binding:i+2,resource:t.createView()})),{binding:7,resource:sampler}]})
    const vertex=device.createShaderModule({code:shader.vert}),fragment=device.createShaderModule({code:shader.frag})
    for(const module of [vertex,fragment]){const info=await module.getCompilationInfo();const errors=info.messages.filter(m=>m.type==='error');if(errors.length)throw Error(errors.map(e=>e.message).join('\n'))}
    const pipeline=await device.createRenderPipelineAsync({layout:device.createPipelineLayout({bindGroupLayouts:[layout]}),vertex:{module:vertex,entryPoint:'VSMain'},fragment:{module:fragment,entryPoint:'PSMain',targets:[{format:navigator.gpu.getPreferredCanvasFormat()}]},primitive:{topology:'triangle-list',cullMode:'none'},depthStencil:{format:'depth32float',depthWriteEnabled:true,depthCompare:'less'}})
    let depth,disposed=false
    return {
      triangles:data.vertices.length/36,
      draw(camera,clear,width,height,lighting,light){
        if(disposed)throw Error('Model has been disposed')
        if(!depth||depth.width!==width||depth.height!==height){depth?.destroy();depth=device.createTexture({size:[width,height],format:'depth32float',usage:GPUTextureUsage.RENDER_ATTACHMENT})}
        device.queue.writeBuffer(uniform,0,sceneUniforms(data,camera,width/height,lighting,light))
        const encoder=device.createCommandEncoder();const pass=encoder.beginRenderPass({colorAttachments:[{view:context.getCurrentTexture().createView(),clearValue:clear,loadOp:'clear',storeOp:'store'}],depthStencilAttachment:{view:depth.createView(),depthClearValue:1,depthLoadOp:'clear',depthStoreOp:'discard'}})
        pass.setPipeline(pipeline);pass.setBindGroup(0,bindGroup);pass.draw(data.vertices.length/12);pass.end();device.queue.submit([encoder.finish()])
      },
      dispose(){if(disposed)return;disposed=true;depth?.destroy();for(const r of resources)r.destroy()},
    }
  }catch(e){for(const r of resources)r.destroy();throw e}
  finally{for(const image of data.images)image.bitmap?.close()}
}
