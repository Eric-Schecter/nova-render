import { lightRecords } from './Lighting.js'
import { copyFog, copyShadows } from './Atmosphere.js'
import { materialMipmaps } from './TextureFiltering.js'
import { createAnimationPlayer } from './AnimationData.js'
import { createPathView } from './PathView.js'
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
  let instanceBuffer,
    sceneDraws = null,
    rasterGlobals = 0,
    rasterFog = copyFog(),
    rasterShadows = copyShadows(),
    shadowInstanceCount = 0
  let depth,
    cube,
    backgroundCube,
    disposed = false,
    environment = null,
    skyPipeline,
    pathView
  try {
    const animator = createAnimationPlayer(data)
    const response = await fetch(shaderURL)
    if (!response.ok) throw Error('Missing model shader package: ' + shaderURL)
    const shader = await response.json()
    const makeBuffer = (bytes, usage, label) => {
      const buffer = device.createBuffer({
        label,
        size: Math.max(48, bytes.byteLength),
        usage: usage | GPUBufferUsage.COPY_DST,
      })
      resources.push(buffer)
      if (bytes.byteLength) device.queue.writeBuffer(buffer, 0, bytes)
      return buffer
    }
    const vertices = makeBuffer(data.vertices, GPUBufferUsage.STORAGE, 'Nova bind vertices'),
      uniform = makeBuffer(new Float32Array(96), GPUBufferUsage.UNIFORM, 'Nova scene uniforms'),
      skin = makeBuffer(
        data.skinInputs || new Uint8Array(64),
        GPUBufferUsage.STORAGE,
        'Nova skin inputs',
      ),
      joints = makeBuffer(
        data.jointMatrices || new Float32Array(16),
        GPUBufferUsage.STORAGE,
        'Nova joint palette',
      ),
      textures = []
    for (const [slot, image] of data.images.entries()) {
      const levels = materialMipmaps(image, slot)
      const texture = device.createTexture({
        size: [image.width, image.height],
        mipLevelCount: levels.length,
        format: slot === 0 || slot === 4 ? 'rgba8unorm-srgb' : 'rgba8unorm',
        usage:
          GPUTextureUsage.TEXTURE_BINDING |
          GPUTextureUsage.COPY_DST |
          GPUTextureUsage.RENDER_ATTACHMENT,
      })
      resources.push(texture)
      textures.push(texture)
      for (const [mipLevel, mip] of levels.entries())
        device.queue.writeTexture(
          { texture, mipLevel },
          mip.pixels,
          { bytesPerRow: mip.width * 4 },
          [mip.width, mip.height],
        )
    }
    instanceBuffer = makeBuffer(new Float32Array(48), GPUBufferUsage.STORAGE, 'Nova instances')
    cube = cubeTexture(device, fallbackEnvironment)
    backgroundCube = cubeTexture(device, fallbackEnvironment)
    const previousUniform = makeBuffer(
        new Float32Array(96),
        GPUBufferUsage.UNIFORM,
        'Nova previous scene',
      ),
      previousPalette = makeBuffer(
        data.jointMatrices || new Float32Array(16),
        GPUBufferUsage.STORAGE,
        'Nova previous palette',
      )
    let lastScene, lastPalette, currentScene, aaPipelines
    let rayProvider, pathData
    const layout = device.createBindGroupLayout({
      entries: [
        {
          binding: 15,
          visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
          buffer: { type: 'uniform', minBindingSize: 384 },
        },
        { binding: 16, visibility: GPUShaderStage.VERTEX, buffer: { type: 'read-only-storage' } },
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
        { binding: 10, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'float' } },
        { binding: 11, visibility: GPUShaderStage.VERTEX, buffer: { type: 'read-only-storage' } },
        { binding: 12, visibility: GPUShaderStage.VERTEX, buffer: { type: 'read-only-storage' } },
        {
          binding: 14,
          visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
          buffer: { type: 'read-only-storage' },
        },
      ],
    })
    const sampler = device.createSampler({
      minFilter: 'linear',
      magFilter: 'linear',
      mipmapFilter: 'linear',
      maxAnisotropy: 8,
      addressModeU: 'repeat',
      addressModeV: 'repeat',
    })
    const group = (texture, background) =>
      device.createBindGroup({
        layout,
        entries: [
          { binding: 15, resource: { buffer: previousUniform } },
          { binding: 16, resource: { buffer: previousPalette } },
          { binding: 0, resource: { buffer: uniform } },
          { binding: 1, resource: { buffer: vertices } },
          ...textures.map((t, i) => ({ binding: i + 2, resource: t.createView() })),
          { binding: 7, resource: sampler },
          { binding: 8, resource: texture.createView({ dimension: 'cube' }) },
          { binding: 9, resource: background.createView({ dimension: 'cube' }) },
          { binding: 10, resource: textures[0].createView() },
          { binding: 11, resource: { buffer: skin } },
          { binding: 12, resource: { buffer: joints } },
          { binding: 14, resource: { buffer: instanceBuffer } },
        ],
      })
    let bindGroup = group(cube, backgroundCube)
    async function pipelineFor(shader, sky = false, samples = 1, motion = false, aa = false) {
      const vertex = device.createShaderModule({ code: shader.vert }),
        fragment = device.createShaderModule({ code: shader.frag })
      for (const module of [vertex, fragment]) {
        const info = await module.getCompilationInfo()
        const errors = info.messages.filter((m) => m.type === 'error')
        if (errors.length) throw Error(errors.map((e) => e.message).join('\n'))
      }
      return device.createRenderPipelineAsync({
        layout: device.createPipelineLayout({ bindGroupLayouts: [layout] }),
        vertex: {
          module: vertex,
          entryPoint: motion ? (sky ? 'SkyVSMain' : 'MotionVSMain') : sky ? 'SkyVSMain' : 'VSMain',
        },
        fragment: {
          module: fragment,
          entryPoint: motion
            ? sky
              ? 'SkyMotionPSMain'
              : 'MotionPSMain'
            : sky
              ? 'SkyPSMain'
              : 'PSMain',
          targets: [{ format: aa ? 'rgba16float' : navigator.gpu.getPreferredCanvasFormat() }],
        },
        primitive: { topology: 'triangle-list', cullMode: 'none' },
        multisample: { count: samples },
        depthStencil: {
          format: 'depth32float',
          depthWriteEnabled: !sky,
          depthCompare: sky ? 'always' : 'less',
        },
      })
    }
    const pipeline = await pipelineFor(shader)
    let rasterLights = null,
      cachedBytes = new Float32Array(0),
      cachedShadows = new Float32Array(0)
    let encodedLights = new Float32Array(0)
    function writeGlobals() {
      const record = new Float32Array(48)
      record.set([...rasterFog.color, ['off', 'linear', 'exp2'].indexOf(rasterFog.mode)], 32)
      record.set([rasterFog.nearDistance, rasterFog.farDistance, rasterFog.density, 0], 36)
      record.set(
        [
          shadowInstanceCount,
          rasterShadows.enabled ? 1 : 0,
          rasterShadows.softness,
          rasterShadows.samples,
        ],
        40,
      )
      record[44] = rasterShadows.bias
      record[45] = encodedLights.length / 48
      record[46] = rasterGlobals + 1 + cachedShadows.byteLength / 192
      record[47] = rasterLights === null ? 0 : 1
      device.queue.writeBuffer(instanceBuffer, rasterGlobals * 192, record)
    }
    function uploadInstances(bytes, draws, shadowRecords = new Float32Array(0)) {
      cachedBytes = bytes
      cachedShadows = shadowRecords
      shadowInstanceCount = draws ? draws.reduce((n, d) => n + d.instanceCount, 0) : 0
      rasterGlobals = bytes.byteLength / 192
      const size = Math.max(
        192,
        bytes.byteLength + 192 + shadowRecords.byteLength + encodedLights.byteLength,
      )
      if (size > device.limits.maxStorageBufferBindingSize)
        throw Error('Scene shadow resources exceed device storage buffer limit')
      if (instanceBuffer.size < size) {
        instanceBuffer.destroy()
        instanceBuffer = makeBuffer(
          new Uint8Array(size),
          GPUBufferUsage.STORAGE,
          'Nova instances and raster settings',
        )
        bindGroup = group(cube, backgroundCube)
      }
      if (bytes.byteLength) device.queue.writeBuffer(instanceBuffer, 0, bytes)
      if (shadowRecords.byteLength)
        device.queue.writeBuffer(instanceBuffer, bytes.byteLength + 192, shadowRecords)
      if (encodedLights.byteLength)
        device.queue.writeBuffer(
          instanceBuffer,
          bytes.byteLength + 192 + shadowRecords.byteLength,
          encodedLights,
        )
      sceneDraws = draws
      writeGlobals()
    }
    uploadInstances(data.materialRecords || new Float32Array(0), null)
    return {
      data,
      async prepareAA(camera, width, height, lighting, light, settings, frames, postSettings) {
        if (!aaPipelines) {
          const dir = shaderURL.slice(0, shaderURL.lastIndexOf('/') + 1)
          const packages = await Promise.all(
            ['model-aa', 'sky-aa', 'model-motion', 'sky-motion'].map(async (n) => {
              const r = await fetch(dir + n + '.json')
              if (!r.ok) throw Error('Missing raster AA shader ' + n)
              return r.json()
            }),
          )
          aaPipelines = {}
          for (const count of [1, 4]) {
            aaPipelines['model' + count] = await pipelineFor(packages[0], false, count, false, true)
            aaPipelines['sky' + count] = await pipelineFor(packages[1], true, count, false, true)
          }
          aaPipelines.motion = await pipelineFor(packages[2], false, 1, true, true)
          aaPipelines.skyMotion = await pipelineFor(packages[3], true, 1, true, true)
        }
        currentScene = sceneUniforms(
          data,
          camera,
          width / height,
          lighting,
          light,
          environment,
          settings,
        )
        currentScene[35] = sceneDraws !== null ? 1 : data.materialRecords?.length ? 2 : 0
        currentScene[75] = rasterGlobals + 1
        currentScene[63] = postSettings?.enabled ? 1 : 0
        device.queue.writeBuffer(uniform, 0, currentScene)
        device.queue.writeBuffer(previousUniform, 0, frames && lastScene ? lastScene : currentScene)
        if (data.animation) device.queue.writeBuffer(joints, 0, data.jointMatrices)
        const palette = data.jointMatrices || new Float32Array(16)
        device.queue.writeBuffer(previousPalette, 0, frames && lastPalette ? lastPalette : palette)
      },
      drawAA(pass, motion, count, settings) {
        pass.setBindGroup(0, bindGroup)
        if (environment && settings.showBackground) {
          pass.setPipeline(motion ? aaPipelines.skyMotion : aaPipelines['sky' + count])
          pass.draw(3)
        }
        if (data.vertices.length) {
          pass.setPipeline(motion ? aaPipelines.motion : aaPipelines['model' + count])
          if (sceneDraws !== null) {
            for (const draw of sceneDraws)
              if (draw.instanceCount)
                pass.draw(
                  draw.vertexCount,
                  draw.instanceCount,
                  draw.firstVertex,
                  draw.firstInstance,
                )
          } else pass.draw(data.vertices.length / 12)
        }
      },
      finishAA() {
        lastScene = currentScene?.slice()
        lastPalette = data.jointMatrices?.slice()
      },
      triangles: data.vertices.length / 36,
      get skinned() {
        return !!data.animation
      },
      get animations() {
        return animator.clips
      },
      get animationState() {
        return animator.state
      },
      setAnimationState(value) {
        animator.set(value)
      },
      updateAnimation(delta) {
        animator.update(delta)
      },
      get accumulatedSamples() {
        return pathView?.samples || 0
      },
      resetAccumulation() {
        pathView?.reset()
      },
      setRaySource(provider) {
        rayProvider = provider
      },
      async drawPath(
        camera,
        clear,
        width,
        height,
        lighting,
        light,
        settings,
        path,
        ao,
        collection = null,
        shadows = undefined,
      ) {
        if (disposed) throw Error('Model has been disposed')
        const source = rayProvider ? rayProvider() : data
        if (!source.vertices.length) throw Error('Path tracing requires a loaded model')
        if (!pathView || pathData !== source) {
          const next = await createPathView(
            device,
            source,
            shaderURL.slice(0, shaderURL.lastIndexOf('/') + 1),
          )
          if (disposed) {
            next.dispose()
            throw Error('Model has been disposed')
          }
          pathView?.dispose()
          pathView = next
          pathData = source
        }
        pathView.draw(
          context,
          camera,
          clear,
          width,
          height,
          lighting,
          light,
          environment,
          settings,
          path,
          ao,
          collection,
          shadows,
        )
      },
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
          nextCube = cubeTexture(device, next || fallbackEnvironment)
          nextBackgroundCube = cubeTexture(device, {
            mips: next ? next.backgroundMips || next.mips : fallbackEnvironment.mips,
          })
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
      setRasterSettings(fog, shadows, lights = null) {
        if (rasterLights !== lights) {
          rasterLights = lights
          encodedLights = lightRecords(lights)
          uploadInstances(cachedBytes, sceneDraws, cachedShadows)
        }
        rasterFog = copyFog(fog)
        rasterShadows = copyShadows(shadows)
        writeGlobals()
      },
      setInstances(bytes, draws, shadowRecords) {
        if (disposed) throw Error('Model has been disposed')
        uploadInstances(bytes, draws, shadowRecords)
      },
      draw(camera, clear, width, height, lighting, light, settings) {
        if (disposed) throw Error('Model has been disposed')
        if (data.animation) device.queue.writeBuffer(joints, 0, data.jointMatrices)
        if (!depth || depth.width !== width || depth.height !== height) {
          depth?.destroy()
          depth = device.createTexture({
            size: [width, height],
            format: 'depth32float',
            usage: GPUTextureUsage.RENDER_ATTACHMENT,
          })
        }
        const values = sceneUniforms(
          data,
          camera,
          width / height,
          lighting,
          light,
          environment,
          settings,
        )
        values[35] = sceneDraws !== null ? 1 : data.materialRecords?.length ? 2 : 0
        values[75] = rasterGlobals + 1
        device.queue.writeBuffer(uniform, 0, values)
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
          if (sceneDraws !== null) {
            for (const draw of sceneDraws)
              if (draw.instanceCount)
                pass.draw(
                  draw.vertexCount,
                  draw.instanceCount,
                  draw.firstVertex,
                  draw.firstInstance,
                )
          } else pass.draw(data.vertices.length / 12)
        }
        pass.end()
        device.queue.submit([encoder.finish()])
      },
      dispose() {
        if (disposed) return
        disposed = true
        pathView?.dispose()
        for (const image of data.images) image.bitmap?.close()
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
    for (const image of data.images) image.bitmap?.close()
    throw error
  }
}
