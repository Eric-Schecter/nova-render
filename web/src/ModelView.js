import { loadGLB, sceneUniforms } from './ModelData.js'
import { fallbackEnvironment } from './EnvironmentData.js'
function cubeTexture(device, data) {
  const texture = device.createTexture({
    size: [data.mips[0].size, data.mips[0].size, 6],
    mipLevelCount: data.mips.length,
    format: 'rgba16float',
    usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
  })
  try {
    data.mips.forEach((mip, level) => {
      const faceBytes = mip.size * mip.size * 8
      for (let face = 0; face < 6; face++)
        device.queue.writeTexture(
          { texture, mipLevel: level, origin: [0, 0, face] },
          mip.pixels.subarray(face * faceBytes, (face + 1) * faceBytes),
          { bytesPerRow: mip.size * 8 },
          [mip.size, mip.size, 1],
        )
    })
    return texture
  } catch (error) {
    texture.destroy()
    throw error
  }
}
export async function createModelView(device, context, url, shaderURL, suppliedData) {
  const data = suppliedData || (await loadGLB(url)),
    resources = []
  let depth,
    cube,
    backgroundCube,
    disposed = false,
    environment = null,
    skyPipeline
  try {
    const response = await fetch(shaderURL)
    if (!response.ok) throw Error('Missing model shader package: ' + shaderURL)
    const shader = await response.json()
    const makeBuffer = (bytes, usage) => {
      const buffer = device.createBuffer({
        size: Math.max(48, bytes.byteLength),
        usage: usage | GPUBufferUsage.COPY_DST,
      })
      resources.push(buffer)
      if (bytes.byteLength) device.queue.writeBuffer(buffer, 0, bytes)
      return buffer
    }
    const vertices = makeBuffer(data.vertices, GPUBufferUsage.STORAGE),
      uniform = makeBuffer(new Float32Array(96), GPUBufferUsage.UNIFORM),
      textures = []
    for (const image of data.images) {
      const texture = device.createTexture({
        size: [image.width, image.height],
        format: 'rgba8unorm',
        usage:
          GPUTextureUsage.TEXTURE_BINDING |
          GPUTextureUsage.COPY_DST |
          GPUTextureUsage.RENDER_ATTACHMENT,
      })
      resources.push(texture)
      textures.push(texture)
      if (image.bitmap)
        device.queue.copyExternalImageToTexture(
          { source: image.bitmap, flipY: false },
          { texture },
          [image.width, image.height],
        )
      else
        device.queue.writeTexture({ texture }, image.pixels, { bytesPerRow: image.width * 4 }, [
          image.width,
          image.height,
        ])
    }
    cube = cubeTexture(device, fallbackEnvironment)
    backgroundCube = cubeTexture(device, fallbackEnvironment)
    const layout = device.createBindGroupLayout({
      entries: [
        {
          binding: 0,
          visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
          buffer: { type: 'uniform', minBindingSize: 384 },
        },
        { binding: 1, visibility: GPUShaderStage.VERTEX, buffer: { type: 'read-only-storage' } },
        ...textures.map((_, i) => ({
          binding: i + 2,
          visibility: GPUShaderStage.FRAGMENT,
          texture: { sampleType: 'float' },
        })),
        { binding: 7, visibility: GPUShaderStage.FRAGMENT, sampler: { type: 'filtering' } },
        {
          binding: 8,
          visibility: GPUShaderStage.FRAGMENT,
          texture: { sampleType: 'float', viewDimension: 'cube' },
        },
        {
          binding: 9,
          visibility: GPUShaderStage.FRAGMENT,
          texture: { sampleType: 'float', viewDimension: 'cube' },
        },
      ],
    })
    const sampler = device.createSampler({
      minFilter: 'linear',
      magFilter: 'linear',
      mipmapFilter: 'linear',
      addressModeU: 'repeat',
      addressModeV: 'repeat',
    })
    const group = (texture, background) =>
      device.createBindGroup({
        layout,
        entries: [
          { binding: 0, resource: { buffer: uniform } },
          { binding: 1, resource: { buffer: vertices } },
          ...textures.map((t, i) => ({ binding: i + 2, resource: t.createView() })),
          { binding: 7, resource: sampler },
          { binding: 8, resource: texture.createView({ dimension: 'cube' }) },
          { binding: 9, resource: background.createView({ dimension: 'cube' }) },
        ],
      })
    let bindGroup = group(cube, backgroundCube)
    async function pipelineFor(shader, sky = false) {
      const vertex = device.createShaderModule({ code: shader.vert }),
        fragment = device.createShaderModule({ code: shader.frag })
      for (const module of [vertex, fragment]) {
        const info = await module.getCompilationInfo()
        const errors = info.messages.filter((m) => m.type === 'error')
        if (errors.length) throw Error(errors.map((e) => e.message).join('\n'))
      }
      return device.createRenderPipelineAsync({
        layout: device.createPipelineLayout({ bindGroupLayouts: [layout] }),
        vertex: { module: vertex, entryPoint: sky ? 'SkyVSMain' : 'VSMain' },
        fragment: {
          module: fragment,
          entryPoint: sky ? 'SkyPSMain' : 'PSMain',
          targets: [{ format: navigator.gpu.getPreferredCanvasFormat() }],
        },
        primitive: { topology: 'triangle-list', cullMode: 'none' },
        depthStencil: {
          format: 'depth32float',
          depthWriteEnabled: !sky,
          depthCompare: sky ? 'always' : 'less',
        },
      })
    }
    const pipeline = await pipelineFor(shader)
    return {
      triangles: data.vertices.length / 36,
      async loadEnvironment(next, skyURL) {
        if (disposed) throw Error('Model has been disposed')
        device.pushErrorScope('validation')
        let nextCube,
          nextBackgroundCube,
          nextGroup,
          nextPipeline = skyPipeline,
          failure
        try {
          if (!nextPipeline) {
            const response = await fetch(skyURL)
            if (!response.ok) throw Error('Missing skybox shader package: ' + skyURL)
            nextPipeline = await pipelineFor(await response.json(), true)
          }
          nextCube = cubeTexture(device, next)
          nextBackgroundCube = cubeTexture(device, { mips: next.backgroundMips || next.mips })
          nextGroup = group(nextCube, nextBackgroundCube)
          await device.queue.onSubmittedWorkDone()
        } catch (error) {
          failure = error
        }
        const error = await device.popErrorScope()
        if (failure || error || disposed) {
          nextCube?.destroy()
          nextBackgroundCube?.destroy()
          throw failure || Error(error?.message || 'Model has been disposed')
        }
        cube.destroy()
        backgroundCube.destroy()
        backgroundCube = nextBackgroundCube
        cube = nextCube
        bindGroup = nextGroup
        skyPipeline = nextPipeline
        environment = next
      },
      draw(camera, clear, width, height, lighting, light, settings) {
        if (disposed) throw Error('Model has been disposed')
        if (!depth || depth.width !== width || depth.height !== height) {
          depth?.destroy()
          depth = device.createTexture({
            size: [width, height],
            format: 'depth32float',
            usage: GPUTextureUsage.RENDER_ATTACHMENT,
          })
        }
        device.queue.writeBuffer(
          uniform,
          0,
          sceneUniforms(data, camera, width / height, lighting, light, environment, settings),
        )
        const encoder = device.createCommandEncoder(),
          pass = encoder.beginRenderPass({
            colorAttachments: [
              {
                view: context.getCurrentTexture().createView(),
                clearValue: clear,
                loadOp: 'clear',
                storeOp: 'store',
              },
            ],
            depthStencilAttachment: {
              view: depth.createView(),
              depthClearValue: 1,
              depthLoadOp: 'clear',
              depthStoreOp: 'discard',
            },
          })
        pass.setBindGroup(0, bindGroup)
        if (environment && settings.showBackground) {
          pass.setPipeline(skyPipeline)
          pass.draw(3)
        }
        if (data.vertices.length) {
          pass.setPipeline(pipeline)
          pass.draw(data.vertices.length / 12)
        }
        pass.end()
        device.queue.submit([encoder.finish()])
      },
      dispose() {
        if (disposed) return
        disposed = true
        depth?.destroy()
        cube?.destroy()
        backgroundCube?.destroy()
        for (const resource of resources) resource.destroy()
      },
    }
  } catch (error) {
    cube?.destroy()
    backgroundCube?.destroy()
    for (const resource of resources) resource.destroy()
    throw error
  } finally {
    for (const image of data.images) image.bitmap?.close()
  }
}
