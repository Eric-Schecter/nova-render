import {
  defaultRasterSettings,
  copyRasterSettings,
  rasterCapabilities,
  gBufferViews,
} from './RasterPipeline.js'
export { modelRasterPipelines, gBufferViews } from './RasterPipeline.js'
import { copyFog, copyShadows } from './Atmosphere.js'
import { packMaterials, validateMaterial, materialCapabilities } from './MaterialData.js'
export { loadMaterialImage } from './MaterialData.js'
import { copyViewCamera, cameraFromOrbit, copyViewport, resolveViewport } from './Camera.js'
import { createViewportSurface } from './ViewportSurface.js'
export { cameraFromOrbit } from './Camera.js'
import { defaultPostProcessing, copyPostProcessing } from './PostProcessing.js'
import { createAntiAliasingView } from './AntiAliasingView.js'
import { defaultAmbientOcclusion, copyAmbientOcclusion } from './AmbientOcclusion.js'
import { defaultPathTracing, validatePathTracing } from './PathView.js'
import { defaultEnvironment, copyEnvironment, loadEnvironmentFile } from './EnvironmentData.js'
import { attachParameterPanel } from './ParameterPanel.js'
import { defaultDirectionalLight, copyDirectionalLight, copySceneLights } from './Lighting.js'
import { attachOrbitControls } from './OrbitControls.js'
import { createModelView } from './ModelView.js'
import { SceneData } from './SceneData.js'
import { validateCamera, emptyModelData } from './ModelData.js'
// Bind the same orbit interaction to an application-owned viewport or view group.
export { attachOrbitControls }
// Browser adapter. Native C++ backends and the web adapter share concepts, not binaries.
export async function createRenderer({ canvas, shaderDirectory = '/generated/' }) {
  if (!navigator.gpu)
    throw Error('WebGPU is unavailable. Use a supported browser over HTTPS or localhost.')
  const adapter = await navigator.gpu.requestAdapter()
  if (!adapter) throw Error('No WebGPU adapter available')
  const device = await adapter.requestDevice()
  const context = canvas.getContext('webgpu')
  if (!context) {
    device.destroy()
    throw Error('Cannot create a WebGPU canvas context')
  }
  return new Renderer(canvas, device, context, shaderDirectory)
}
class Renderer {
  #canvas
  #device
  #context
  #outputContext
  #surface
  #surfaceFrame = false
  #sceneCanvas = { width: 1, height: 1 }
  #viewCamera
  #viewport = null
  #pipeline
  #triangleShader
  #triangleAA = {}
  #aaView
  #rasterSettings = defaultRasterSettings()
  getRasterSettings() {
    this.#check()
    return { ...this.#rasterSettings }
  }
  getRasterCapabilities() {
    this.#check()
    return rasterCapabilities()
  }
  setRasterSettings(value) {
    this.#check()
    const next = copyRasterSettings({ ...this.#rasterSettings, ...value })
    if (next.pipeline === 'deferred' && this.#aaSettings.method === 'msaa')
      throw Error('Deferred MSAA is not supported')
    if (next.pipeline === 'deferred' && this.#aoSettings.enabled)
      throw Error('Deferred cannot use the separate BVH AO preview path')
    this.#rasterSettings = next
    this.#resetAA()
    this.#parameterPanel?.refresh()
  }
  getMaterialCapabilities() {
    this.#check()
    return materialCapabilities()
  }
  #hasBlend(data = this.#model?.data) {
    return data?.materials?.some((m) => m.alphaMode === 'BLEND') || false
  }
  #hasAlpha(data = this.#model?.data) {
    return data?.materials?.some((m) => (m.alphaMode || 'OPAQUE') !== 'OPAQUE') || false
  }
  #validateMaterials(data) {
    if (this.#hasAlpha(data) && (this.#renderMode === 'ray-tracing' || this.#aoSettings.enabled))
      throw Error('Alpha materials currently require raster rendering')
    if (
      this.#hasBlend(data) &&
      (['msaa', 'taa'].includes(this.#aaSettings.method) || this.#aaSettings.split)
    )
      throw Error('BLEND does not support MSAA, TAA or AA split')
  }
  #postSettings = defaultPostProcessing()
  #postRemaining = 0
  getPostProcessing() {
    this.#check()
    return { ...this.#postSettings }
  }
  setPostProcessing(value) {
    this.#check()
    const next = copyPostProcessing({ ...this.#postSettings, ...value })
    if (next.enabled !== this.#postSettings.enabled) this.#resetAA()
    this.#postSettings = next
    this.#aaView?.setPostProcessing(next)
    this.#postRemaining = next.enabled && next.autoExposure ? 480 : 0
  }
  #aaRemaining = 0
  #aaSettings = { method: 'off', historyWeight: 0.9, zoom: 1, split: false, textureFiltering: true }
  #resetAA() {
    this.#aaView?.resetHistory()
    this.#aaRemaining = 32
    if (this.#postSettings.enabled && this.#postSettings.autoExposure) this.#postRemaining = 480
  }
  #taaActive() {
    return (
      this.#renderMode === 'raster' &&
      !this.#aoSettings.enabled &&
      this.#rasterSettings.view === 'final' &&
      this.#aaSettings.method === 'taa' &&
      this.#aaRemaining > 0
    )
  }
  getAntiAliasing() {
    this.#check()
    return { ...this.#aaSettings }
  }
  getAntiAliasingHistoryFrames() {
    return this.#aaSettings.method === 'taa' && this.#rasterSettings.view === 'final'
      ? this.#aaView?.getHistoryFrames() || 0
      : 0
  }
  setAntiAliasing(value) {
    this.#check()
    const next = { ...this.#aaSettings, ...value }
    if (
      !['off', 'fxaa', 'smaa', 'msaa', 'ssaa', 'taa'].includes(next.method) ||
      !Number.isFinite(next.historyWeight) ||
      next.historyWeight < 0 ||
      next.historyWeight > 0.98 ||
      !Number.isFinite(next.zoom) ||
      next.zoom < 1 ||
      next.zoom > 8 ||
      typeof next.split !== 'boolean' ||
      typeof next.textureFiltering !== 'boolean'
    )
      throw Error('Invalid AA settings')
    if (this.#rasterSettings.pipeline === 'deferred' && next.method === 'msaa')
      throw Error('Deferred MSAA is not supported')
    if (this.#hasBlend() && (['msaa', 'taa'].includes(next.method) || next.split))
      throw Error('BLEND does not support MSAA, TAA or AA split')
    this.#aaSettings = next
    this.#aaView?.setAntiAliasing(next)
    this.#resetAA()
    this.#parameterPanel?.refresh()
  }
  async #drawAA(width, height) {
    if (!this.#model && this.#rasterSettings.pipeline === 'deferred')
      throw Error('Deferred requires a model or scene')
    if (!this.#aaView) {
      const scene = {
          get raster() {
            return owner.#rasterSettings
          },
          get alphaBlend() {
            return owner.#hasBlend()
          },
          get encoded() {
            return !owner.#model
          },
          clear: () =>
            this.#clear.map((v, i) =>
              i === 3 || !this.#model ? v : v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4,
            ),
          prepare: async (w, h, settings, frames) => {
            if (this.#model)
              await this.#model.prepareAA(
                this.getViewCamera(),
                w,
                h,
                this.#lighting,
                this.#directionalLight,
                this.#environmentSettings,
                frames,
                this.#postSettings,
                this.#rasterSettings,
              )
            else if (!this.#triangleAA[settings.method === 'msaa' ? 4 : 1]) {
              const count = settings.method === 'msaa' ? 4 : 1,
                shader = this.#triangleShader
              this.#triangleAA[count] = await this.#device.createRenderPipelineAsync({
                layout: 'auto',
                vertex: {
                  module: this.#device.createShaderModule({ code: shader.vert }),
                  entryPoint: 'VSMain',
                },
                fragment: {
                  module: this.#device.createShaderModule({ code: shader.frag }),
                  entryPoint: 'PSMain',
                  targets: [{ format: 'rgba16float' }],
                },
                primitive: { topology: 'triangle-list' },
                multisample: { count },
                depthStencil: {
                  format: 'depth32float',
                  depthWriteEnabled: false,
                  depthCompare: 'always',
                },
              })
            }
          },
          draw: (pass, motion, count, program, gbuffers) => {
            if (this.#model)
              this.#model.drawAA(pass, motion, count, this.#environmentSettings, program, gbuffers)
            else {
              pass.setPipeline(this.#triangleAA[count])
              pass.draw(3)
            }
          },
          finish: () => this.#model?.finishAA(),
        },
        owner = this
      this.#aaView = await createAntiAliasingView({
        canvas: this.#sceneCanvas,
        shaderDirectory: this.#shaderDirectory,
        device: this.#device,
        context: this.#context,
        scene,
      })
      this.#aaView.setAntiAliasing(this.#aaSettings)
      this.#aaView.setPostProcessing(this.#postSettings)
    }
    await this.#aaView.drawFrame(width, height)
    this.#aaRemaining = Math.max(0, this.#aaRemaining - 1)
    this.#postRemaining = Math.max(0, this.#postRemaining - 1)
  }
  #renderMode = 'raster'
  #pathSettings = defaultPathTracing()
  #aoSettings = defaultAmbientOcclusion()
  #continuous = true
  #pathFrame = 0
  #pathError
  #animationLastTime
  #configured = false
  #disposed = false
  #lost
  #clear = [0.025, 0.035, 0.055, 1]
  #parameterPanel
  #orbitControls
  #renderTail = Promise.resolve()
  #lighting = 'unlit'
  #lights = null
  #directionalLight = defaultDirectionalLight()
  #environment
  #sceneData = new SceneData()
  #sceneModel
  #modelURL
  #environmentURL
  #environmentSettings = defaultEnvironment()
  #fogSettings = copyFog()
  #shadowSettings = copyShadows()
  #model
  #shaderDirectory
  #camera = { yaw: 0, pitch: 0, distance: 3.6, exposure: 1 }
  constructor(canvas, device, context, shaderDirectory) {
    this.#shaderDirectory = shaderDirectory
    this.#canvas = canvas
    this.#device = device
    this.#outputContext = context
    this.#context = {
      configure: (settings) =>
        this.#surfaceFrame
          ? this.#surface.context.configure(settings)
          : context.configure(settings),
      getCurrentTexture: () =>
        this.#surfaceFrame
          ? this.#surface.context.getCurrentTexture()
          : context.getCurrentTexture(),
    }
    device.lost.then((info) => {
      this.#lost = info.message || 'GPU device lost'
    })
  }
  #check() {
    if (this.#disposed) throw Error('Renderer has been disposed')
    if (this.#lost) throw Error(this.#lost)
  }
  #enqueue(action) {
    this.#check()
    const operation = this.#renderTail.then(action)
    this.#renderTail = operation.catch(() => {})
    return operation
  }
  /** Synchronous CPU ray query of the last committed scene; null on miss. */
  pickScene(ray) {
    this.#check()
    return this.#sceneData.pick(ray, !!this.#sceneModel)
  }
  getSceneAssetBounds(id) {
    this.#check()
    return this.#sceneModel ? this.#sceneData.assetBounds(id) : null
  }
  /** Replace static raster instances. Assets: {id,url,sourceSpace?}; instances: {id,assetId,world?}. */
  setScene(assets, instances) {
    assets = structuredClone(assets)
    instances = structuredClone(instances)
    return this.#enqueue(async () => {
      this.#check()
      if (this.#aoSettings.enabled)
        throw Error('Disable ambient occlusion before setting scene instances')
      const nextScene = this.#sceneData.fork()
      const data = await nextScene.update(assets, instances)
      this.#validateMaterials(data.model)
      this.#check()
      if (this.#sceneModel !== data.model || !this.#model) {
        const next = await createModelView(
          this.#device,
          this.#context,
          null,
          this.#shaderDirectory.replace(/\/?$/, '/') + 'model.json',
          data.model,
        )
        try {
          if (this.#environment)
            await next.loadEnvironment(
              this.#environment,
              this.#shaderDirectory.replace(/\/?$/, '/') + 'skybox.json',
            )
          this.#check()
        } catch (e) {
          next.dispose()
          throw e
        }
        this.#model?.dispose()
        this.#model = next
        this.#sceneModel = data.model
      }
      this.#model.setInstances(data.instances, data.draws, data.shadowRecords)
      this.#model.setRaySource(() => nextScene.rayModel())
      this.#sceneData = nextScene
      this.#modelURL = undefined
      this.#animationLastTime = undefined
      this.#resetAA()
      this.#parameterPanel?.refresh()
    })
  }
  getMaterials(assetId = '') {
    this.#check()
    if (this.#sceneModel) return this.#sceneData.getMaterials(assetId)
    if (assetId || !this.#model) throw Error('No model material slots')
    return structuredClone(this.#model.data.materials || [])
  }
  setMaterial(slot, material, assetId = '') {
    material = validateMaterial(material)
    return this.#enqueue(async () => {
      this.#check()
      let sceneData, draws, instances, data, shadowRecords
      if (this.#sceneModel) {
        sceneData = this.#sceneData.fork()
        const packed = await sceneData.setMaterial(assetId, slot, material)
        ;({ draws, instances, shadowRecords } = packed)
        data = packed.model
      } else {
        if (
          assetId ||
          !this.#model ||
          !Number.isInteger(slot) ||
          slot < 0 ||
          slot >= this.#model.data.materials.length
        )
          throw Error('Unknown model material slot')
        data = { ...this.#model.data, materials: [...this.#model.data.materials] }
        data.materials[slot] = material
        packMaterials(data)
      }
      this.#validateMaterials(data)
      const animationState = this.#model.animationState
      const next = await createModelView(
        this.#device,
        this.#context,
        null,
        this.#shaderDirectory.replace(/\/?$/, '/') + 'model.json',
        data,
      )
      try {
        this.#validateMaterials(next.data)
        if (this.#environment)
          await next.loadEnvironment(
            this.#environment,
            this.#shaderDirectory.replace(/\/?$/, '/') + 'skybox.json',
          )
        if (sceneData) {
          next.setInstances(instances, draws, shadowRecords)
          next.setRaySource(() => sceneData.rayModel())
        }
        if (next.skinned) next.setAnimationState(animationState)
        this.#check()
      } catch (e) {
        next.dispose()
        throw e
      }
      this.#model.dispose()
      this.#model = next
      if (sceneData) {
        this.#sceneData = sceneData
        this.#sceneModel = data
      }
      this.#resetAA()
      this.#parameterPanel?.refresh()
    })
  }
  loadModel(url) {
    return this.#enqueue(async () => {
      this.#check()
      this.#device.pushErrorScope('validation')
      let next, failure
      try {
        next = await createModelView(
          this.#device,
          this.#context,
          url,
          this.#shaderDirectory.replace(/\/?$/, '/') + 'model.json',
        )
        this.#validateMaterials(next.data)
        if (this.#environment)
          await next.loadEnvironment(
            this.#environment,
            this.#shaderDirectory.replace(/\/?$/, '/') + 'skybox.json',
          )
        await this.#device.queue.onSubmittedWorkDone()
      } catch (error) {
        failure = error
      }
      const error = await this.#device.popErrorScope()
      if (failure || error || this.#disposed) {
        next?.dispose()
        throw failure || Error(error?.message || 'Renderer has been disposed')
      }
      this.#model?.dispose()
      this.#resetAA()
      this.#model = next
      this.#animationLastTime = undefined
      if (next.skinned) this.#renderMode = 'raster'
      this.#sceneModel = undefined
      this.#sceneData.dispose()
      this.#modelURL = url
      this.#parameterPanel?.refresh()
      return { triangles: next.triangles }
    })
  }
  loadEnvironment(url) {
    return this.#enqueue(async () => {
      this.#check()
      const environment = url ? await loadEnvironmentFile(url) : null
      this.#check()
      let candidate = this.#model
      try {
        if (!candidate)
          candidate = await createModelView(
            this.#device,
            this.#context,
            null,
            this.#shaderDirectory.replace(/\/?$/, '/') + 'model.json',
            emptyModelData(),
          )
        await candidate.loadEnvironment(
          environment,
          this.#shaderDirectory.replace(/\/?$/, '/') + 'skybox.json',
        )
        this.#check()
      } catch (error) {
        if (candidate !== this.#model) candidate?.dispose()
        throw error
      }
      this.#resetAA()
      this.#model = candidate
      this.#environment = environment
      this.#environmentURL = url
      this.#parameterPanel?.refresh()
    })
  }
  getCapabilities() {
    this.#check()
    if (this.#hasAlpha())
      return {
        rayTracing: false,
        pathTracing: false,
        softwareRayTracing: false,
        metropolis: false,
        rayTracingDevice: '',
        reason: 'Alpha materials currently require raster rendering',
      }
    if (this.#model?.skinned)
      return {
        rayTracing: false,
        pathTracing: false,
        softwareRayTracing: false,
        metropolis: false,
        rayTracingDevice: '',
        reason: 'Animated models currently use raster rendering',
      }
    return {
      rayTracing: this.#device.limits.maxStorageBuffersPerShaderStage >= 8,
      pathTracing: this.#device.limits.maxStorageBuffersPerShaderStage >= 8,
      softwareRayTracing: true,
      metropolis: this.#device.limits.maxStorageBuffersPerShaderStage >= 8,
      rayTracingDevice: 'WebGPU compute BVH',
      reason:
        this.#device.limits.maxStorageBuffersPerShaderStage >= 8
          ? ''
          : 'At least eight storage buffers are required.',
    }
  }
  getRenderMode() {
    this.#check()
    return this.#renderMode
  }
  setRenderMode(mode) {
    this.#check()
    if (mode === 'path-tracing') mode = 'ray-tracing'
    if (!['raster', 'ray-tracing'].includes(mode)) throw Error('Invalid render mode')
    if (mode !== 'raster') {
      if (!this.getCapabilities().pathTracing) throw Error(this.getCapabilities().reason)
      if (!this.#sceneModel && !this.#model?.triangles)
        throw Error('Path tracing requires a loaded model')
    }
    if (mode === this.#renderMode) return
    this.#resetAA()
    this.#renderMode = mode
    this.resetAccumulation()
    if (this.#pathFrame) cancelAnimationFrame(this.#pathFrame)
    this.#pathFrame = 0
    this.#parameterPanel?.refresh()
  }
  getAnimations() {
    this.#check()
    return this.#model?.animations || []
  }
  getAnimationState() {
    this.#check()
    return this.#model?.animationState || { clip: 0, time: 0, speed: 1, playing: true, loop: true }
  }
  setAnimationState(value) {
    this.#check()
    if (!this.#model?.animations.length) throw Error('No animation clips loaded')
    const old = this.#model.animationState
    if (
      (value.clip !== undefined && value.clip !== old.clip) ||
      (value.time !== undefined && Math.abs(value.time - old.time) > 0.25)
    )
      this.#resetAA()
    this.#aaRemaining = 32
    if (this.#postSettings.enabled && this.#postSettings.autoExposure) this.#postRemaining = 480
    this.#model.setAnimationState(value)
    this.#animationLastTime = undefined
    this.#parameterPanel?.refresh()
    this.#continuePathTracing()
  }
  updateAnimation(delta) {
    this.#check()
    if (!Number.isFinite(delta) || delta < 0) throw Error('Invalid animation delta')
    this.#model?.updateAnimation(delta)
  }
  #animationPlaying() {
    return !!this.#model?.animations.length && this.#model.animationState.playing
  }
  setAmbientOcclusion(value) {
    this.#check()
    const next = copyAmbientOcclusion(value)
    if (next.enabled && this.#rasterSettings.pipeline === 'deferred')
      throw Error('Deferred cannot use the separate BVH AO preview path')
    if (next.enabled && !this.getCapabilities().rayTracing)
      throw Error(this.getCapabilities().reason)
    this.#resetAA()
    this.#aoSettings = next
    this.resetAccumulation()
    this.#parameterPanel?.refresh()
  }
  getAmbientOcclusion() {
    this.#check()
    return { ...this.#aoSettings }
  }
  getRayTracing() {
    return this.getPathTracing()
  }
  setRayTracing(settings) {
    this.setPathTracing(settings)
  }
  setPathTracing(settings) {
    this.#check()
    validatePathTracing(settings)
    this.#pathSettings = {
      ...settings,
      denoise: settings.denoise ?? false,
      lightSampling: settings.lightSampling ?? true,
      integrator: settings.integrator ?? 'path',
      photonsPerIteration: settings.photonsPerIteration ?? 8192,
      photonRadius: settings.photonRadius ?? 0.16,
      mltBootstrapSamples: settings.mltBootstrapSamples ?? 262144,
      mltLargeStepProbability: settings.mltLargeStepProbability ?? 0.3,
      mltMutationScale: settings.mltMutationScale ?? 0.01,
      mltSeed: settings.mltSeed ?? 1,
    }
    this.resetAccumulation()
    this.#parameterPanel?.refresh()
  }
  getPathTracing() {
    this.#check()
    return { ...this.#pathSettings }
  }
  resetAccumulation() {
    this.#check()
    this.#model?.resetAccumulation()
    this.#pathError = undefined
  }
  getAccumulatedSamples() {
    this.#check()
    return this.#model?.accumulatedSamples || 0
  }
  setContinuousRendering(enabled = true) {
    this.#check()
    if (typeof enabled !== 'boolean') throw Error('Continuous rendering requires a boolean')
    this.#continuous = enabled
    if (this.#pathFrame) cancelAnimationFrame(this.#pathFrame)
    this.#pathFrame = 0
    if (enabled) this.#continuePathTracing()
  }
  // Only one scheduled frame, requested after the previous GPU submission finishes.
  #continuePathTracing() {
    if (
      !this.#continuous ||
      this.#disposed ||
      this.#pathError ||
      (this.#renderMode === 'raster' &&
        !this.#aoSettings.enabled &&
        !this.#animationPlaying() &&
        !this.#taaActive() &&
        (this.#postRemaining <= 0 || this.#rasterSettings.view !== 'final')) ||
      this.#pathFrame
    )
      return
    this.#pathFrame = requestAnimationFrame((timestamp) => {
      this.#pathFrame = 0
      if (
        this.#disposed ||
        (this.#renderMode === 'raster' &&
          !this.#aoSettings.enabled &&
          !this.#animationPlaying() &&
          !this.#taaActive() &&
          (this.#postRemaining <= 0 || this.#rasterSettings.view !== 'final'))
      )
        return
      const rect = this.#canvas.getBoundingClientRect()
      if (
        document.hidden ||
        this.#canvas.closest('[aria-hidden="true"], [inert]') ||
        !rect.width ||
        !rect.height ||
        rect.bottom < 0 ||
        rect.top > innerHeight
      ) {
        this.#animationLastTime = undefined
        this.#continuePathTracing()
        return
      }
      if (this.#animationPlaying()) {
        const delta =
          this.#animationLastTime === undefined
            ? 0
            : Math.min(0.1, (timestamp - this.#animationLastTime) / 1000)
        this.#animationLastTime = timestamp
        this.updateAnimation(delta)
      }
      this.drawFrame().catch((error) => {
        this.#pathError = error
        this.#canvas.dispatchEvent(new CustomEvent('nova-error', { detail: error }))
      })
    })
  }
  setShadows(settings) {
    this.#check()
    const next = copyShadows({ ...this.#shadowSettings, ...settings })
    this.#shadowSettings = next
    this.#resetAA()
  }
  getShadows() {
    this.#check()
    return copyShadows(this.#shadowSettings)
  }
  setFog(settings) {
    this.#check()
    const next = copyFog({ ...this.#fogSettings, ...settings })
    this.#fogSettings = next
    this.#resetAA()
  }
  getFog() {
    this.#check()
    return copyFog(this.#fogSettings)
  }
  setEnvironment(settings) {
    this.#check()
    this.#resetAA()
    this.#environmentSettings = copyEnvironment(settings)
    this.#parameterPanel?.refresh()
  }
  getEnvironment() {
    this.#check()
    return { ...this.#environmentSettings }
  }
  setLights(lights) {
    this.#check()
    const capabilities = this.getLightingCapabilities()
    if (!capabilities.maxLights) throw Error(capabilities.reason)
    const next = copySceneLights(lights)
    this.#lights = next
    this.#resetAA()
    this.resetAccumulation()
    this.#parameterPanel?.refresh()
  }
  getLights() {
    this.#check()
    return copySceneLights(
      this.#lights ?? [
        {
          id: '__directional',
          ...this.#directionalLight,
          castShadow: true,
          shadowSoftness: this.#shadowSettings.softness,
        },
      ],
    )
  }
  getLightingCapabilities() {
    this.#check()
    const supported = this.#renderMode === 'raster' || this.getCapabilities().pathTracing
    return {
      directional: supported,
      point: supported,
      spot: supported,
      maxLights: supported ? 16 : 0,
      directionalShadows: supported,
      pointShadows: supported,
      spotShadows: supported,
      shadowRequiresScene: this.#renderMode === 'raster',
      collectionLightSubpaths: supported && this.#renderMode !== 'raster',
      collectionPhotonEmission: supported && this.#renderMode !== 'raster',
      sourceLightTypes: supported && this.#renderMode !== 'raster' ? ['point', 'spot'] : [],
      estimator:
        this.#renderMode === 'raster'
          ? this.#rasterSettings.pipeline === 'deferred'
            ? 'deferred-pbr'
            : 'raster-pbr'
          : 'camera-and-source-paths',
      reason: supported ? '' : 'Current device or scene does not support ray tracing',
    }
  }
  setDirectionalLight(light) {
    this.#check()
    this.#resetAA()
    const next = copyDirectionalLight(light)
    this.#lights = null
    this.#directionalLight = next
    this.resetAccumulation()
    this.#parameterPanel?.refresh()
  }
  getDirectionalLight() {
    this.#check()
    return copyDirectionalLight(this.#directionalLight)
  }
  setLighting(mode) {
    this.#check()
    if (mode !== 'unlit' && mode !== 'pbr')
      throw Error('Invalid lighting mode: expected unlit or pbr')
    this.#resetAA()
    this.#lighting = mode
    this.#parameterPanel?.refresh()
  }
  getViewCamera() {
    this.#check()
    return copyViewCamera(this.#viewCamera || cameraFromOrbit(this.#camera))
  }
  setViewCamera(value) {
    this.#check()
    if (!value || typeof value !== 'object' || Array.isArray(value))
      throw Error('Camera settings must be an object')
    const camera = copyViewCamera({ ...this.getViewCamera(), ...value })
    this.disableOrbitControls()
    if (this.#viewCamera && JSON.stringify(this.#viewCamera) === JSON.stringify(camera)) return
    this.#viewCamera = camera
    this.#resetAA()
    this.resetAccumulation()
  }
  getViewport() {
    this.#check()
    return this.#viewport ? { ...this.#viewport } : null
  }
  setViewport(value) {
    this.#check()
    const next = copyViewport(value)
    if (JSON.stringify(this.#viewport) === JSON.stringify(next)) return
    this.#viewport = next
    this.#resetAA()
    this.resetAccumulation()
  }
  setCamera(camera) {
    this.#check()
    const value = { ...this.#camera, ...camera }
    validateCamera(value)
    if (this.#viewCamera) this.#resetAA()
    this.#viewCamera = undefined
    if (
      Math.abs(value.yaw - this.#camera.yaw) > 20 ||
      Math.abs(value.pitch - this.#camera.pitch) > 20 ||
      Math.abs(value.distance - this.#camera.distance) > 2 ||
      value.exposure !== this.#camera.exposure ||
      JSON.stringify(value.target || [0, 0, 0]) !== JSON.stringify(this.#camera.target || [0, 0, 0])
    )
      this.#resetAA()
    this.#aaRemaining = 32
    if (this.#postSettings.enabled && this.#postSettings.autoExposure) this.#postRemaining = 480
    this.#camera = { ...value, target: [...(value.target || [0, 0, 0])] }
    this.#parameterPanel?.refresh()
  }
  getCamera() {
    this.#check()
    return { ...this.#camera, target: [...(this.#camera.target || [0, 0, 0])] }
  }
  enableParameterPanel(options = {}) {
    this.#check()
    this.disableParameterPanel()
    const panel = attachParameterPanel(
      this.#canvas,
      {
        getRasterSettings: () => this.getRasterSettings(),
        setRasterSettings: (value) => this.setRasterSettings(value),
        getAntiAliasing: () => this.getAntiAliasing(),
        setAntiAliasing: (value) => this.setAntiAliasing(value),
        getAmbientOcclusion: () => this.getAmbientOcclusion(),
        getCapabilities: () => this.getCapabilities(),
        getRenderMode: () => this.getRenderMode(),
        setRenderMode: (mode) => this.setRenderMode(mode),
        getAnimations: () => this.getAnimations(),
        getAnimationState: () => this.getAnimationState(),
        setAnimationState: (value) => this.setAnimationState(value),
        getPathTracing: () => this.getPathTracing(),
        setPathTracing: (settings) => this.setPathTracing(settings),
        getAccumulatedSamples: () => this.getAccumulatedSamples(),
        resetAccumulation: () => this.resetAccumulation(),
        getAssets: () => ({ model: this.#modelURL, environment: this.#environmentURL }),
        loadModel: (url) => this.loadModel(url),
        loadEnvironment: (url) => this.loadEnvironment(url),
        getCamera: () => this.getCamera(),
        setCamera: (value) => this.setCamera(value),
        getLight: () => this.getDirectionalLight(),
        setLight: (value) => {
          if (this.#lights === null) this.setDirectionalLight(value)
        },
        getClear: () => this.getClearColor(),
        setClear: (value) => this.setClearColor(value),
        getEnvironment: () => this.getEnvironment(),
        setEnvironment: (value) => this.setEnvironment(value),
        getMode: () => ({
          camera: !!this.#model,
          environment: !!this.#environment,
          lighting:
            !!this.#model && (this.#lighting === 'pbr' || this.#renderMode === 'ray-tracing'),
          pathTracing: this.#renderMode !== 'raster',
          animation: this.getAnimations()
            .map((c) => c.name)
            .join('|'),
        }),
        drawFrame: () => this.drawFrame(),
      },
      options,
    )
    this.#parameterPanel = panel
    return () => {
      panel.dispose()
      if (this.#parameterPanel === panel) this.#parameterPanel = undefined
    }
  }
  disableParameterPanel() {
    this.#parameterPanel?.dispose()
    this.#parameterPanel = undefined
  }
  getClearColor() {
    this.#check()
    return [...this.#clear]
  }
  enableOrbitControls(options = {}) {
    this.#check()
    this.disableOrbitControls()
    const controls = attachOrbitControls(this.#canvas, {
      ...options,
      getCamera: () => this.getCamera(),
      setCamera: (camera) => this.setCamera(camera),
      drawFrame: () => this.drawFrame(),
    })
    this.#orbitControls = controls
    return () => {
      controls.dispose()
      if (this.#orbitControls === controls) this.#orbitControls = undefined
    }
  }
  disableOrbitControls() {
    this.#orbitControls?.dispose()
    this.#orbitControls = undefined
  }
  async setShader({ source, vert, frag }) {
    this.#check()
    // Generated packages carry source + WGSL. Edited HLSL is compiled locally.
    if (!vert || !frag) {
      const response = await fetch('/__nova/compile', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ source }),
      })
      const data = await response.json()
      if (!response.ok) throw Error(data.error || 'HLSL compilation failed')
      vert = data.vert
      frag = data.frag
    }
    this.#check()
    const device = this.#device
    device.pushErrorScope('validation')
    let pipeline, failure
    try {
      const vertex = device.createShaderModule({ code: vert }),
        fragment = device.createShaderModule({ code: frag })
      for (const module of [vertex, fragment]) {
        const info = await module.getCompilationInfo()
        const errors = info.messages.filter((m) => m.type === 'error')
        if (errors.length)
          throw Error(errors.map((m) => `${m.lineNum}:${m.linePos} ${m.message}`).join('\n'))
      }
      pipeline = await device.createRenderPipelineAsync({
        layout: 'auto',
        vertex: { module: vertex, entryPoint: 'VSMain' },
        fragment: {
          module: fragment,
          entryPoint: 'PSMain',
          targets: [{ format: navigator.gpu.getPreferredCanvasFormat() }],
        },
        primitive: { topology: 'triangle-list' },
      })
    } catch (e) {
      failure = e
    }
    const error = await device.popErrorScope()
    if (failure || error) throw failure || Error(error.message)
    this.#check()
    await this.waitIdle()
    this.#model?.dispose()
    this.#model = undefined
    this.#sceneModel = undefined
    this.#sceneData.dispose()
    this.#environment = undefined
    this.#environmentURL = undefined
    this.#modelURL = undefined
    this.#resetAA()
    this.#triangleShader = { vert, frag }
    this.#triangleAA = {}
    this.#pipeline = pipeline
    this.#renderMode = 'raster'
    if (this.#pathFrame) cancelAnimationFrame(this.#pathFrame)
    this.#pathFrame = 0
    this.#parameterPanel?.refresh()
  }
  setClearColor(color) {
    this.#check()
    if (
      !Array.isArray(color) ||
      color.length !== 4 ||
      !color.every((v) => Number.isFinite(v) && v >= 0 && v <= 1)
    )
      throw Error('Clear color requires four numbers in [0,1]')
    this.#resetAA()
    this.#clear = [...color]
    this.#parameterPanel?.refresh()
  }
  drawFrame() {
    this.#check()
    const frame = this.#renderTail.then(() => this.#drawFrame())
    this.#renderTail = frame.catch(() => {})
    return frame
  }
  async #drawFrame() {
    this.#check()
    this.#model?.setRasterSettings(this.#fogSettings, this.#shadowSettings, this.#lights)
    if (!this.#pipeline && !this.#model) throw Error('Call setShader or loadModel first')
    const rect = this.#canvas.getBoundingClientRect()
    if (!rect.width || !rect.height) return 'skipped'
    const ratio = window.devicePixelRatio || 1
    const width = Math.min(
      this.#device.limits.maxTextureDimension2D,
      Math.max(1, Math.round(rect.width * ratio)),
    )
    const height = Math.min(
      this.#device.limits.maxTextureDimension2D,
      Math.max(1, Math.round(rect.height * ratio)),
    )
    if (this.#canvas.width !== width) this.#canvas.width = width
    if (this.#canvas.height !== height) this.#canvas.height = height
    if (!this.#configured) {
      this.#outputContext.configure({
        device: this.#device,
        format: navigator.gpu.getPreferredCanvasFormat(),
        alphaMode: 'opaque',
      })
      this.#configured = true
    }
    const viewport = resolveViewport(this.#viewport, width, height)
    if (!viewport.width || !viewport.height) return 'skipped'
    this.#surfaceFrame =
      this.#renderMode === 'ray-tracing' ||
      viewport.x !== 0 ||
      viewport.y !== 0 ||
      viewport.width !== width ||
      viewport.height !== height
    if (this.#surfaceFrame) {
      if (!this.#surface)
        this.#surface = await createViewportSurface(this.#device, this.#shaderDirectory)
      this.#check()
      this.#surface.resize(viewport.width, viewport.height)
    }
    this.#device.pushErrorScope('validation')
    let failure
    try {
      if (
        this.#renderMode === 'raster' &&
        !this.#aoSettings.enabled &&
        (this.#aaSettings.method !== 'off' ||
          this.#postSettings.enabled ||
          this.#rasterSettings.pipeline === 'deferred' ||
          this.#hasBlend())
      )
        await this.#drawAA(viewport.width, viewport.height)
      else if (this.#model) {
        if (this.#renderMode === 'ray-tracing' || this.#aoSettings.enabled)
          await this.#model.drawPath(
            this.getViewCamera(),
            this.#clear,
            viewport.width,
            viewport.height,
            this.#lighting,
            this.#directionalLight,
            this.#environmentSettings,
            this.#pathSettings,
            this.#renderMode === 'ray-tracing'
              ? { ...this.#aoSettings, enabled: false }
              : this.#aoSettings,
            this.#lights,
            this.#shadowSettings,
          )
        else
          this.#model.draw(
            this.getViewCamera(),
            this.#clear,
            viewport.width,
            viewport.height,
            this.#lighting,
            this.#directionalLight,
            this.#environmentSettings,
          )
      } else {
        const encoder = this.#device.createCommandEncoder()
        const pass = encoder.beginRenderPass({
          colorAttachments: [
            {
              view: this.#context.getCurrentTexture().createView(),
              clearValue: this.#clear,
              loadOp: 'clear',
              storeOp: 'store',
            },
          ],
        })
        pass.setPipeline(this.#pipeline)
        pass.draw(3)
        pass.end()
        this.#device.queue.submit([encoder.finish()])
      }
      if (this.#surfaceFrame) this.#surface.present(this.#outputContext, viewport, this.#clear)
      await this.#device.queue.onSubmittedWorkDone()
    } catch (e) {
      failure = e
    }
    const error = await this.#device.popErrorScope()
    if (failure || error) throw failure || Error(error.message)
    this.#pathError = undefined
    this.#parameterPanel?.refresh()
    this.#continuePathTracing()
    return 'rendered'
  }
  async waitIdle() {
    this.#check()
    await this.#renderTail
    this.#check()
    await this.#device.queue.onSubmittedWorkDone()
  }
  dispose() {
    if (this.#disposed) return
    this.disableParameterPanel()
    this.disableOrbitControls()
    this.#disposed = true
    this.#sceneData.dispose()
    if (this.#pathFrame) cancelAnimationFrame(this.#pathFrame)
    this.#pathFrame = 0
    this.#aaView?.dispose()
    this.#model?.dispose()
    this.#surface?.dispose()
    this.#outputContext.unconfigure()
    this.#device.destroy()
  }
}

export { createAntiAliasingView } from './AntiAliasingView.js'

export { createShadowView } from './ShadowView.js'

export { createPostProcessingView } from './PostProcessingView.js'

export { createScreenSpaceReflectionView } from './ScreenSpaceReflectionView.js'
export { createPlanarReflectionView } from './PlanarReflectionView.js'
export { createReflectionProbeView } from './ReflectionProbeView.js'

export {
  createPipelineComparisonView,
  rasterPipelines,
  pipelineDebugViews,
} from './PipelineComparisonView.js'
