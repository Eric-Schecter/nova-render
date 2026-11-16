// Texture-backed context: AA/path passes render locally, then present into one viewport.
// Keeping a texture (rather than another canvas swapchain) also survives async shader setup.
export async function createViewportSurface(device, shaderDirectory) {
  const format = navigator.gpu.getPreferredCanvasFormat()
  const response = await fetch(shaderDirectory.replace(/\/?$/, '/') + 'viewport-present.json')
  if (!response.ok) throw Error('Missing viewport presentation shader')
  const shader = await response.json()
  const pipeline = await device.createRenderPipelineAsync({
    layout: 'auto',
    vertex: { module: device.createShaderModule({ code: shader.vert }), entryPoint: 'VSMain' },
    fragment: {
      module: device.createShaderModule({ code: shader.frag }),
      entryPoint: 'PSMain',
      targets: [{ format }],
    },
    primitive: { topology: 'triangle-list' },
  })
  const sampler = device.createSampler({ minFilter: 'nearest', magFilter: 'nearest' })
  let texture,
    group,
    disposed = false
  const surface = { width: 1, height: 1 }
  function resize(width, height) {
    if (disposed) throw Error('Viewport surface disposed')
    if (texture?.width === width && texture?.height === height) return
    texture?.destroy()
    texture = device.createTexture({
      size: [width, height],
      format,
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING,
    })
    group = device.createBindGroup({
      layout: pipeline.getBindGroupLayout(0),
      entries: [
        { binding: 0, resource: texture.createView() },
        { binding: 1, resource: sampler },
      ],
    })
  }
  const context = {
    configure() {
      resize(surface.width, surface.height)
    },
    getCurrentTexture() {
      resize(surface.width, surface.height)
      return texture
    },
  }
  return {
    canvas: surface,
    context,
    resize(width, height) {
      surface.width = width
      surface.height = height
      resize(width, height)
    },
    present(output, rect, clear) {
      const e = device.createCommandEncoder(),
        pass = e.beginRenderPass({
          colorAttachments: [
            {
              view: output.getCurrentTexture().createView(),
              clearValue: clear,
              loadOp: 'clear',
              storeOp: 'store',
            },
          ],
        })
      pass.setPipeline(pipeline)
      pass.setBindGroup(0, group)
      pass.setViewport(rect.x, rect.y, rect.width, rect.height, 0, 1)
      pass.setScissorRect(rect.x, rect.y, rect.width, rect.height)
      pass.draw(3)
      pass.end()
      device.queue.submit([e.finish()])
    },
    dispose() {
      disposed = true
      texture?.destroy()
    },
  }
}
