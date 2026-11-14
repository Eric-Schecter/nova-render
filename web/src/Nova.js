import { defaultPathTracing, validatePathTracing } from './PathView.js'
import { defaultEnvironment, copyEnvironment, loadEnvironmentFile } from './EnvironmentData.js'
import { attachParameterPanel } from './ParameterPanel.js'
import { defaultDirectionalLight, copyDirectionalLight } from './Lighting.js'
import { attachOrbitControls } from './OrbitControls.js'
import { createModelView } from './ModelView.js'
import { validateCamera, emptyModelData } from './ModelData.js'
// Browser adapter. Native C++ backends and the web adapter share concepts, not binaries.
export async function createRenderer({ canvas, shaderDirectory = '/generated/' }) {
  if (!navigator.gpu) throw Error('此浏览器未启用 WebGPU，请使用支持的浏览器及 HTTPS / localhost。')
  const adapter = await navigator.gpu.requestAdapter()
  if (!adapter) throw Error('没有可用的 WebGPU 适配器')
  const device = await adapter.requestDevice()
  const context = canvas.getContext('webgpu')
  if (!context) {
    device.destroy()
    throw Error('无法创建 WebGPU canvas')
  }
  return new Renderer(canvas, device, context, shaderDirectory)
}
class Renderer {
  #canvas
  #device
  #context
  #pipeline
  #renderMode = 'raster'
  #pathSettings = defaultPathTracing()
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
  #directionalLight = defaultDirectionalLight()
  #environment
  #modelURL
  #environmentURL
  #environmentSettings = defaultEnvironment()
  #model
  #shaderDirectory
  #camera = { yaw: 0, pitch: 0, distance: 3.6, exposure: 1 }
  constructor(canvas, device, context, shaderDirectory) {
    this.#shaderDirectory = shaderDirectory
    this.#canvas = canvas
    this.#device = device
    this.#context = context
    device.lost.then((info) => {
      this.#lost = info.message || 'GPU device lost'
    })
  }
  #check() {
    if (this.#disposed) throw Error('Renderer 已释放')
    if (this.#lost) throw Error(this.#lost)
  }
  #enqueue(action) {
    this.#check()
    const operation = this.#renderTail.then(action)
    this.#renderTail = operation.catch(() => {})
    return operation
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
        throw failure || Error(error?.message || 'Renderer 已释放')
      }
      this.#model?.dispose()
      this.#model = next
      this.#animationLastTime = undefined
      if (next.skinned) this.#renderMode = 'raster'
      this.#modelURL = url
      this.#parameterPanel?.refresh()
      return { triangles: next.triangles }
    })
  }
  loadEnvironment(url) {
    return this.#enqueue(async () => {
      this.#check()
      const environment = await loadEnvironmentFile(url)
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
      this.#model = candidate
      this.#environment = environment
      this.#environmentURL = url
      this.#parameterPanel?.refresh()
    })
  }
  getCapabilities() {
    this.#check()
    if (this.#model?.skinned)
      return {
        pathTracing: false,
        softwareRayTracing: false,
        rayTracingDevice: '',
        reason: 'Animated models currently use raster rendering',
      }
    return {
      pathTracing: this.#device.limits.maxStorageBuffersPerShaderStage >= 8,
      softwareRayTracing: true,
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
    if (mode !== 'raster' && mode !== 'path-tracing') throw Error('Invalid render mode')
    if (mode === 'path-tracing') {
      if (!this.getCapabilities().pathTracing) throw Error(this.getCapabilities().reason)
      if (!this.#model?.triangles) throw Error('Path tracing requires a loaded model')
    }
    if (mode === this.#renderMode) return
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
  setPathTracing(settings) {
    this.#check()
    validatePathTracing(settings)
    this.#pathSettings = {
      ...settings,
      denoise: settings.denoise ?? false,
      lightSampling: settings.lightSampling ?? true,
      integrator: settings.integrator ?? 'path',
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
  // Only one scheduled frame, requested after the previous GPU submission finishes.
  #continuePathTracing() {
    if (
      this.#disposed ||
      this.#pathError ||
      (this.#renderMode !== 'path-tracing' && !this.#animationPlaying()) ||
      this.#pathFrame
    )
      return
    this.#pathFrame = requestAnimationFrame((timestamp) => {
      this.#pathFrame = 0
      if (this.#disposed || (this.#renderMode !== 'path-tracing' && !this.#animationPlaying()))
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
  setEnvironment(settings) {
    this.#check()
    this.#environmentSettings = copyEnvironment(settings)
    this.#parameterPanel?.refresh()
  }
  getEnvironment() {
    this.#check()
    return { ...this.#environmentSettings }
  }
  setDirectionalLight(light) {
    this.#check()
    this.#directionalLight = copyDirectionalLight(light)
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
    this.#lighting = mode
    this.#parameterPanel?.refresh()
  }
  setCamera(camera) {
    this.#check()
    const value = { ...this.#camera, ...camera }
    validateCamera(value)
    this.#camera = value
    this.#parameterPanel?.refresh()
  }
  getCamera() {
    this.#check()
    return { ...this.#camera }
  }
  enableParameterPanel(options = {}) {
    this.#check()
    this.disableParameterPanel()
    const panel = attachParameterPanel(
      this.#canvas,
      {
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
        setLight: (value) => this.setDirectionalLight(value),
        getClear: () => this.getClearColor(),
        setClear: (value) => this.setClearColor(value),
        getEnvironment: () => this.getEnvironment(),
        setEnvironment: (value) => this.setEnvironment(value),
        getMode: () => ({
          camera: !!this.#model,
          environment: !!this.#environment,
          lighting:
            !!this.#model && (this.#lighting === 'pbr' || this.#renderMode === 'path-tracing'),
          pathTracing: this.#renderMode === 'path-tracing',
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
      if (!response.ok) throw Error(data.error || 'HLSL 编译失败')
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
    this.#environment = undefined
    this.#environmentURL = undefined
    this.#modelURL = undefined
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
      throw Error('背景色需要 4 个 0～1 的数值')
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
    if (!this.#pipeline && !this.#model) throw Error('请先调用 setShader 或 loadModel')
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
      this.#context.configure({
        device: this.#device,
        format: navigator.gpu.getPreferredCanvasFormat(),
        alphaMode: 'opaque',
      })
      this.#configured = true
    }
    this.#device.pushErrorScope('validation')
    let failure
    try {
      if (this.#model) {
        if (this.#renderMode === 'path-tracing')
          await this.#model.drawPath(
            this.#camera,
            this.#clear,
            width,
            height,
            this.#lighting,
            this.#directionalLight,
            this.#environmentSettings,
            this.#pathSettings,
          )
        else
          this.#model.draw(
            this.#camera,
            this.#clear,
            width,
            height,
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
    if (this.#pathFrame) cancelAnimationFrame(this.#pathFrame)
    this.#pathFrame = 0
    this.#model?.dispose()
    this.#context.unconfigure()
    this.#device.destroy()
  }
}
