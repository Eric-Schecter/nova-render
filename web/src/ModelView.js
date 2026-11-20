import { canUseDeferred, canUseHalfReflections } from './RasterPipeline.js'
import { modelVertexFloats, modelTriangleFloats } from './ModelVertex.js'
import { createHybridProbes, probeSceneBounds } from './HybridProbes.js'
import { PickMesh } from './Picking.js'
import { lightRecords } from './Lighting.js'
import { copyFog, copyShadows } from './Atmosphere.js'
import { materialMipmaps } from './TextureFiltering.js'
import { createAnimationPlayer, poseAnimationGeometry } from './AnimationData.js'
import { createPathView } from './PathView.js'
import { loadGLB, sceneUniforms } from './ModelData.js'
import { fallbackEnvironment } from './EnvironmentData.js'

const modelTextureBinding = (i) => (i < 5 ? i + 2 : i + 23)
const gBufferBindings = [17, 18, 19, 20, 21, 22, 23, 24, 31]
const halfGBufferBindings = [...gBufferBindings, 32]
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
    shadowInstanceCount = 0,
    singleCaster = false,
    hybrid = null
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
        size: Math.max(modelVertexFloats * Float32Array.BYTES_PER_ELEMENT, bytes.byteLength),
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
          binding: modelTextureBinding(i),
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
          ...textures.map((t, i) => ({
            binding: modelTextureBinding(i),
            resource: t.createView(),
          })),
          { binding: 7, resource: sampler },
          { binding: 8, resource: texture.createView({ dimension: 'cube' }) },
          { binding: 9, resource: background.createView({ dimension: 'cube' }) },
          { binding: 10, resource: textures[0].createView() },
          { binding: 11, resource: { buffer: skin } },
          { binding: 12, resource: { buffer: joints } },
          { binding: 14, resource: { buffer: instanceBuffer } },
        ],
      })
    const makeGBufferLayout = (bindings) =>
      device.createBindGroupLayout({
        entries: bindings.map((binding) => ({
          binding,
          visibility: GPUShaderStage.FRAGMENT,
          texture: { sampleType: 'float' },
        })),
      })
    const gBufferLayout = makeGBufferLayout(gBufferBindings)
    const opaqueColorLayout = makeGBufferLayout([17])
    const halfGBufferLayout = canUseHalfReflections(device.limits)
      ? makeGBufferLayout(halfGBufferBindings)
      : null
    const probeLayout = device.createBindGroupLayout({
      entries: [
        { binding: 25, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } },
        { binding: 26, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'read-only-storage' } },
      ],
    })
    let probes,
      probeDeferred,
      probeDeferredHalf,
      probeShader,
      probeBounds,
      probeLightingKey,
      probeEnabled
    let bindGroup = group(cube, backgroundCube)
    async function pipelineFor(
      shader,
      sky = false,
      samples = 1,
      motion = false,
      aa = false,
      gbuffer = false,
      deferred = false,
      oit = false,
      hybridShadow = false,
      probe = false,
      halfReflection = false,
      halfCombine = false,
    ) {
      const vertex = device.createShaderModule({ code: shader.vert }),
        fragment = device.createShaderModule({ code: shader.frag })
      for (const module of [vertex, fragment]) {
        const info = await module.getCompilationInfo()
        const errors = info.messages.filter((m) => m.type === 'error')
        if (errors.length) throw Error(errors.map((e) => e.message).join('\n'))
      }
      const surfaceLayout = halfCombine ? halfGBufferLayout : gBufferLayout
      return device.createRenderPipelineAsync({
        layout: device.createPipelineLayout({
          bindGroupLayouts: probe
            ? [layout, surfaceLayout, probeLayout]
            : deferred || hybridShadow || halfReflection
              ? [layout, surfaceLayout]
              : oit
                ? [layout, opaqueColorLayout]
                : [layout],
        }),
        vertex: {
          module: vertex,
          entryPoint:
            deferred || hybridShadow || halfReflection
              ? 'SkyVSMain'
              : motion
                ? sky
                  ? 'SkyVSMain'
                  : 'MotionVSMain'
                : sky
                  ? 'SkyVSMain'
                  : 'VSMain',
        },
        fragment: {
          module: fragment,
          entryPoint: halfReflection
            ? 'HybridReflectionPSMain'
            : hybridShadow
              ? 'HybridShadowPSMain'
              : oit
                ? 'OITPSMain'
                : deferred
                  ? 'DeferredPSMain'
                  : gbuffer
                    ? 'GBufferPSMain'
                    : motion
                      ? sky
                        ? 'SkyMotionPSMain'
                        : 'MotionPSMain'
                      : sky
                        ? 'SkyPSMain'
                        : 'PSMain',
          targets: Array.from({ length: oit ? 2 : gbuffer ? 5 : hybridShadow ? 4 : 1 }, (_, i) => ({
            ...(oit
              ? {
                  blend:
                    i === 0
                      ? {
                          color: { srcFactor: 'one', dstFactor: 'one' },
                          alpha: { srcFactor: 'one', dstFactor: 'one' },
                        }
                      : {
                          color: { srcFactor: 'zero', dstFactor: 'one-minus-src-alpha' },
                          alpha: { srcFactor: 'zero', dstFactor: 'one-minus-src-alpha' },
                        },
                }
              : {}),
            format: aa ? 'rgba16float' : navigator.gpu.getPreferredCanvasFormat(),
          })),
        },
        primitive: { topology: 'triangle-list', cullMode: 'none' },
        multisample: { count: samples },
        ...(halfReflection
          ? {}
          : {
              depthStencil: {
                format: 'depth32float',
                depthWriteEnabled: !sky && !deferred && !oit && !hybridShadow,
                depthCompare:
                  sky || deferred || hybridShadow ? 'always' : oit ? 'less-equal' : 'less',
              },
            }),
      })
    }
    const pipeline = await pipelineFor(shader)
    let rasterLights = null,
      cachedBytes = new Float32Array(0),
      cachedShadows = new Float32Array(0)
    let encodedLights = new Float32Array(0)
    async function updateProbes(lighting, light, environmentSettings) {
      probeEnabled = false
      if (!hybrid?.probeGI) return
      const bounds = hybrid.probeBounds
        ? [hybrid.probeBounds.minimum, hybrid.probeBounds.maximum]
        : (probeBounds ||= probeSceneBounds(data.vertices, cachedBytes, sceneDraws))
      if (!bounds) return
      if (!probes) {
        const response = await fetch(
          new URL('model-probes.json', new URL(shaderURL, document.baseURI)),
        )
        if (!response.ok) throw Error('Missing probe shader package')
        probeShader = await response.json()
        const compute = await device.createComputePipelineAsync({
          layout: 'auto',
          compute: {
            module: device.createShaderModule({ code: probeShader.compute }),
            entryPoint: 'HybridProbeUpdateCS',
          },
        })
        probeDeferred = await pipelineFor(
          probeShader,
          false,
          1,
          false,
          true,
          false,
          true,
          false,
          false,
          true,
        )
        probes = createHybridProbes(device, compute, probeLayout)
      }
      if (hybrid.reflections && hybrid.halfResolutionReflections && !probeDeferredHalf)
        probeDeferredHalf = await pipelineFor(
          { vert: probeShader.vert, frag: probeShader.halfFrag },
          false,
          1,
          false,
          true,
          false,
          true,
          false,
          false,
          true,
          false,
          true,
        )
      const key = JSON.stringify([lighting, light, environmentSettings])
      if (key !== probeLightingKey) probes.invalidate()
      probeLightingKey = key
      probes.update(
        bounds,
        rasterGlobals,
        [
          { binding: 0, resource: { buffer: uniform } },
          ...textures.map((t, i) => ({
            binding: modelTextureBinding(i),
            resource: t.createView(),
          })),
          { binding: 7, resource: sampler },
          { binding: 8, resource: cube.createView({ dimension: 'cube' }) },
          { binding: 14, resource: { buffer: instanceBuffer } },
        ],
        hybrid.probeBounds ? 0 : 0.25,
      )
      probeEnabled = true
    }
    function writeGlobals() {
      const record = new Float32Array(48)
      record.set(
        [
          hybrid?.ambientOcclusion ? hybrid.aoRadius : 0,
          hybrid?.aoSamples || 4,
          hybrid?.aoStrength ?? 1,
          ['final', 'shadows', 'ambient-occlusion', 'reflections'].indexOf(hybrid?.view),
        ],
        0,
      )
      record.set(
        [
          hybrid?.reflections ? hybrid.reflectionDistance : 0,
          hybrid?.reflectionSamples || 1,
          hybrid?.reflectionStrength ?? 1,
          hybrid?.diffuseBounce ? 10 : 0,
        ],
        4,
      )
      record[8] = hybrid?.clearcoatReflections ? 1 : 0
      record[9] = hybrid?.reflectionDenoising && hybrid.reflectionHitReprojection ? 1 : 0
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
      shadowInstanceCount = draws
        ? draws.reduce((n, d) => n + d.instanceCount, 0)
        : singleCaster
          ? 1
          : 0
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
    let singleShadowPose = null
    function refreshSingleShadows() {
      if (sceneDraws !== null) return
      const posed =
        !!data.animation &&
        (rasterShadows.enabled || encodedLights.some((value, i) => i % 48 === 46 && value > 0))
      const enabled = !!hybrid || posed
      const state = posed ? animator.state : null
      const pose = state ? [state.clip, state.time] : null
      if (
        enabled &&
        (!singleCaster ||
          pose?.[0] !== singleShadowPose?.[0] ||
          pose?.[1] !== singleShadowPose?.[1])
      ) {
        const singleBVH = new PickMesh(
          posed ? poseAnimationGeometry(data, data.jointMatrices) : data,
        )
        singleShadowPose = pose
        const bytes = new Float32Array(48 + (data.materialRecords?.length || 0))
        for (const k of [0, 5, 10, 15, 16, 21, 26, 31]) bytes[k] = 1
        bytes[28] = posed ? 1 : 0
        bytes[44] = 1
        bytes[46] = 1
        bytes[47] = singleBVH.nodes.length
        bytes.set(data.materialRecords || [], 48)
        singleCaster = true
        uploadInstances(bytes, null, singleBVH.shadowRecords())
      } else if (!enabled && singleCaster) {
        singleCaster = false
        uploadInstances(data.materialRecords || new Float32Array(0), null)
      }
    }
    return {
      data,
      setHybrid(settings) {
        if (
          hybrid !== settings &&
          (hybrid?.probeGI !== settings?.probeGI ||
            JSON.stringify(hybrid?.probeBounds) !== JSON.stringify(settings?.probeBounds))
        )
          probes?.invalidate()
        hybrid = settings
        writeGlobals()
        refreshSingleShadows()
      },
      async prepareAA(
        camera,
        width,
        height,
        lighting,
        light,
        settings,
        frames,
        postSettings,
        rasterSettings,
      ) {
        if (!aaPipelines) {
          const dir = shaderURL.slice(0, shaderURL.lastIndexOf('/') + 1)
          const packages = await Promise.all(
            [
              'model-aa',
              'sky-aa',
              'model-motion',
              'sky-motion',
              'model-gbuffer',
              'model-deferred',
              'model-oit',
              'model-hybrid-shadow',
            ].map(async (n) => {
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
          if (canUseDeferred(device.limits)) {
            aaPipelines.gbuffer = await pipelineFor(packages[4], false, 1, false, true, true)
            aaPipelines.deferred = await pipelineFor(
              packages[5],
              false,
              1,
              false,
              true,
              false,
              true,
            )
            aaPipelines.hybridShadow = await pipelineFor(
              packages[7],
              false,
              1,
              false,
              true,
              false,
              false,
              false,
              true,
            )
          }
          aaPipelines.oit = await pipelineFor(
            packages[6],
            false,
            1,
            false,
            true,
            false,
            false,
            true,
          )
          aaPipelines.motion = await pipelineFor(packages[2], false, 1, true, true)
          aaPipelines.skyMotion = await pipelineFor(packages[3], true, 1, true, true)
        }
        if (hybrid?.reflections && hybrid.halfResolutionReflections && !aaPipelines.deferredHalf) {
          const dir = shaderURL.slice(0, shaderURL.lastIndexOf('/') + 1)
          const [trace, combine] = await Promise.all(
            ['model-hybrid-reflection', 'model-deferred-half'].map(async (name) => {
              const response = await fetch(dir + name + '.json')
              if (!response.ok) throw Error('Missing raster AA shader ' + name)
              return response.json()
            }),
          )
          aaPipelines.halfReflection = await pipelineFor(
            trace,
            true,
            1,
            false,
            true,
            false,
            false,
            false,
            false,
            false,
            true,
          )
          aaPipelines.deferredHalf = await pipelineFor(
            combine,
            false,
            1,
            false,
            true,
            false,
            true,
            false,
            false,
            false,
            false,
            true,
          )
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
        currentScene[35] =
          sceneDraws !== null || singleCaster ? 1 : data.materialRecords?.length ? 2 : 0
        currentScene[75] = rasterGlobals + 1
        currentScene[67] = hybrid ? 1 : 0
        currentScene[71] = hybrid?.shadowSamples || 1
        currentScene[79] = hybrid?.shadows ? 1 : 0
        const denoiseReflections =
          hybrid?.reflections && hybrid.halfResolutionReflections && hybrid.reflectionDenoising
        // Negative frame indices rotate reflection rays without changing AO or shadow sampling.
        const temporalShadows =
          hybrid?.shadows &&
          hybrid.shadowTemporalReconstruction &&
          hybrid.view === 'final' &&
          rasterSettings?.view === 'final'
        currentScene[87] = hybrid?.temporalReconstruction
          ? frames + 1
          : denoiseReflections
            ? -(frames + 1)
            : 0
        currentScene[83] =
          hybrid?.view === 'shadows' ? hybrid.shadowLight + 1 : temporalShadows ? -(frames + 1) : 0
        currentScene[63] =
          (rasterSettings?.view && rasterSettings.view !== 'final') ||
          (hybrid && hybrid.view !== 'final')
            ? 0
            : postSettings?.enabled
              ? 1
              : 0
        currentScene[91] = rasterSettings?.debugRange || 20
        currentScene[95] = [
          'final',
          'base-color',
          'world-normal',
          'geometric-normal',
          'linear-depth',
          'roughness',
          'metallic',
          'occlusion',
          'emissive',
          'world-position',
        ].indexOf(rasterSettings?.view || 'final')
        device.queue.writeBuffer(uniform, 0, currentScene)
        await updateProbes(lighting, light, settings)
        device.queue.writeBuffer(previousUniform, 0, frames && lastScene ? lastScene : currentScene)
        if (data.skinInputs?.byteLength) device.queue.writeBuffer(joints, 0, data.jointMatrices)
        const palette = data.jointMatrices || new Float32Array(16)
        device.queue.writeBuffer(previousPalette, 0, frames && lastPalette ? lastPalette : palette)
      },
      writeReflectionReprojection(uniforms) {
        const previous = lastScene || currentScene
        uniforms.splice(28, 16, ...previous.subarray(0, 16))
        uniforms.splice(44, 4, ...currentScene.subarray(16, 20))
        uniforms.splice(48, 12, ...currentScene.subarray(40, 52))
        uniforms.splice(60, 4, ...previous.subarray(16, 20))
      },
      drawAA(pass, motion, count, settings, program, gbuffers) {
        if (program === -5 || program === -7 || program === -8) {
          const half = hybrid?.reflections && hybrid.halfResolutionReflections
          const halfCombine = program === -5 && half
          pass.setBindGroup(0, bindGroup)
          pass.setBindGroup(
            1,
            device.createBindGroup({
              layout: halfCombine ? halfGBufferLayout : gBufferLayout,
              entries: (halfCombine ? halfGBufferBindings : gBufferBindings).map((binding, i) => ({
                binding,
                resource: gbuffers[i].createView(),
              })),
            }),
          )
          if (program === -5 && probeEnabled) pass.setBindGroup(2, probes.read)
          pass.setPipeline(
            program === -8
              ? aaPipelines.halfReflection
              : program === -7
                ? aaPipelines.hybridShadow
                : probeEnabled
                  ? half
                    ? probeDeferredHalf
                    : probeDeferred
                  : half
                    ? aaPipelines.deferredHalf
                    : aaPipelines.deferred,
          )
          pass.draw(3)
          return
        }
        const gbuffer = program === -4,
          oit = program === -6
        pass.setBindGroup(0, bindGroup)
        if (oit)
          pass.setBindGroup(
            1,
            device.createBindGroup({
              layout: opaqueColorLayout,
              entries: [{ binding: 17, resource: gbuffers[0].createView() }],
            }),
          )
        if (!gbuffer && !oit && environment && settings.showBackground) {
          pass.setPipeline(motion ? aaPipelines.skyMotion : aaPipelines['sky' + count])
          pass.draw(3)
        }
        if (data.vertices.length) {
          pass.setPipeline(
            oit
              ? aaPipelines.oit
              : gbuffer
                ? aaPipelines.gbuffer
                : motion
                  ? aaPipelines.motion
                  : aaPipelines['model' + count],
          )
          if (sceneDraws !== null) {
            for (const draw of sceneDraws)
              if (draw.instanceCount)
                pass.draw(
                  draw.vertexCount,
                  draw.instanceCount,
                  draw.firstVertex,
                  draw.firstInstance,
                )
          } else pass.draw(data.vertices.length / modelVertexFloats)
        }
      },
      finishAA() {
        lastScene = currentScene?.slice()
        lastPalette = data.jointMatrices?.slice()
      },
      triangles: data.vertices.length / modelTriangleFloats,
      get skinned() {
        return !!data.skinInputs?.byteLength
      },
      get animations() {
        return animator.clips
      },
      get animationState() {
        return animator.state
      },
      setAnimationState(value) {
        animator.set(value)
        refreshSingleShadows()
      },
      updateAnimation(delta) {
        animator.update(delta)
        refreshSingleShadows()
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
        if (data.materials?.some((m) => (m.alphaMode || 'OPAQUE') !== 'OPAQUE'))
          throw Error('Alpha materials currently require raster rendering')
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
        probes?.invalidate()
        backgroundCube = nextBackgroundCube
        cube = nextCube
        bindGroup = nextGroup
        skyPipeline = nextPipeline
        environment = next
      },
      setRasterSettings(fog, shadows, lights = null) {
        if (rasterLights !== lights) {
          probes?.invalidate()
          rasterLights = lights
          encodedLights = lightRecords(lights)
          uploadInstances(cachedBytes, sceneDraws, cachedShadows)
        }
        rasterFog = copyFog(fog)
        rasterShadows = copyShadows(shadows)
        refreshSingleShadows()
        writeGlobals()
      },
      setInstances(bytes, draws, shadowRecords) {
        if (disposed) throw Error('Model has been disposed')
        probeBounds = undefined
        probes?.invalidate()
        uploadInstances(bytes, draws, shadowRecords)
      },
      draw(camera, clear, width, height, lighting, light, settings) {
        if (disposed) throw Error('Model has been disposed')
        if (data.skinInputs?.byteLength) device.queue.writeBuffer(joints, 0, data.jointMatrices)
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
        values[35] = sceneDraws !== null || singleCaster ? 1 : data.materialRecords?.length ? 2 : 0
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
          } else pass.draw(data.vertices.length / modelVertexFloats)
        }
        pass.end()
        device.queue.submit([encoder.finish()])
      },
      dispose() {
        if (disposed) return
        disposed = true
        pathView?.dispose()
        probes?.dispose()
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
