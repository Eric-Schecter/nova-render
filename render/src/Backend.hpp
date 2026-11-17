#pragma once
#include "AnimationData.hpp"
#include "CameraData.hpp"
#include "Color.hpp"
#include "ModelData.hpp"
#include "ParameterPanel.hpp"
#include "SceneData.hpp"
#include "TextureFiltering.hpp"
#include "aa/RasterAA.hpp"
#include "path/PathTracing.hpp"
#include "render/Renderer.hpp"
#include <cmath>
#include <stdexcept>

namespace render::detail
{
    class Backend
    {
      public:
        PanelAssets assets;
        SceneData   scene;
        void        ValidateMaterialState(const ModelData* next) const
        {
            if (mode == RenderMode::Hybrid && next && next->animation)
                throw std::invalid_argument("Hybrid currently requires static geometry");
            if (mode == RenderMode::RayTracing && HasAlpha(next))
                throw std::invalid_argument("MASK/BLEND ray tracing is not supported; select raster first");
            if (
              HasBlend(next)
              && (rasterSettings.method == AAMethod::MSAA || rasterSettings.method == AAMethod::TAA || rasterSettings.split))
                throw std::invalid_argument(
                  "BLEND supports Off/FXAA/SMAA/SSAA without AA split; choose a compatible AA method first");
        }
        HybridSettings     hybrid;
        HybridCapabilities GetHybridCapabilities() const
        {
            HybridCapabilities c;
            if (model && model->animation)
            {
                c.supported = false;
                c.reason    = "Hybrid currently requires static geometry";
            }
            return c;
        }
        void SetHybrid(const HybridSettings& value)
        {
            if (
              value.shadowSamples < 1 || value.shadowSamples > 16 || unsigned(value.view) > 3 || value.shadowLight > 15
              || !std::isfinite(value.aoRadius) || value.aoRadius <= 0 || value.aoRadius > 10000
              || !std::isfinite(value.aoStrength) || value.aoStrength < 0 || value.aoStrength > 4 || value.aoSamples < 1
              || value.aoSamples > 32 || !std::isfinite(value.reflectionDistance) || value.reflectionDistance <= 0
              || value.reflectionDistance > 100000 || !std::isfinite(value.reflectionStrength)
              || value.reflectionStrength < 0 || value.reflectionStrength > 1 || value.reflectionSamples < 1
              || value.reflectionSamples > 16)
                throw std::invalid_argument("Invalid Hybrid settings");
            hybrid = value;
            RebuildRasterInstances();
            rasterAA.Reset();
        }
        HybridSettings GetHybrid() const { return hybrid; }
        void        SetScene(const std::vector<SceneAsset>& assets, const std::vector<SceneInstance>& instances)
        {
            const auto previousMode = mode;
            auto next = scene;
            next.Update(assets, instances);
            ValidateMaterialState(next.model.get());
            if (model != next.model)
                LoadModel(next.model);
            scene      = std::move(next);
            RebuildRasterInstances();
            aaTriangle = false;
            presentedImage.reset();
            pathSession.Reset();
            rasterAA.Reset();
            mode = previousMode;
        }
        std::vector<Material> GetMaterials(const std::string& id) const
        {
            if (scene.active)
                return scene.GetMaterials(id);
            if (!id.empty() || !model)
                throw std::invalid_argument("No model material slots");
            return model->materials;
        }
        void SetMaterial(uint32_t slot, const Material& value, const std::string& id)
        {
            ValidateMaterial(value);
            if (scene.active)
            {
                auto next = scene;
                next.SetMaterial(id, slot, value);
                ValidateMaterialState(next.model.get());
                LoadModel(next.model);
                scene = std::move(next);
            }
            else
            {
                if (!id.empty() || !model || slot >= model->materials.size())
                    throw std::invalid_argument("Unknown model material slot");
                auto next             = std::make_shared<ModelData>(*model);
                next->materials[slot] = value;
                PackMaterials(*next);
                ValidateMaterialState(next.get());
                auto state = animation.state;
                LoadModel(next);
                if (model->animation)
                    SetAnimationState(state);
            }
            RebuildRasterInstances();
            pathSession.Reset();
            rasterAA.Reset();
        }
        bool                    lightCollectionActive = false;
        std::vector<SceneLight> lights;
        LightingCapabilities    GetLightingCapabilities() const
        {
            LightingCapabilities c;
            if ((pathAPI == GraphicsAPI::Metal || pathAPI == GraphicsAPI::Vulkan || pathAPI == GraphicsAPI::D3D12))
            {
                c.directional = c.point = c.spot = c.directionalShadows = c.pointShadows = c.spotShadows = true;
                c.maxLights                                                                              = 16;
                c.shadowRequiresScene = mode == RenderMode::Raster;
                c.estimator           = mode != RenderMode::RayTracing
                                          ? (modelRaster.pipeline == RasterPipeline::Deferred ? "deferred-pbr" : "raster-pbr")
                                          : "camera-and-source-paths";
                if (mode == RenderMode::RayTracing)
                {
                    c.collectionLightSubpaths = c.collectionPhotonEmission = true;
                    c.sourceLightTypes                                     = {LightType::Point, LightType::Spot};
                }
            }
            else
                c.reason = "Light collections require Metal, Vulkan or DX12";
            return c;
        }
        void SetLights(const std::vector<SceneLight>& values)
        {
            if (!GetLightingCapabilities().maxLights)
                throw std::runtime_error(GetLightingCapabilities().reason);
            auto next             = ValidateSceneLights(values);
            lights                = std::move(next);
            lightCollectionActive = true;
            RebuildRasterInstances();
            rasterAA.Reset();
            pathSession.Reset();
        }
        std::vector<SceneLight> GetLights() const
        {
            if (lightCollectionActive)
                return lights;
            SceneLight v;
            v.id             = "__directional";
            v.direction      = directionalLight.direction;
            v.color          = directionalLight.color;
            v.intensity      = directionalLight.intensity;
            v.castShadow     = true;
            v.shadowSoftness = shadowSettings.softness;
            return ValidateSceneLights({v});
        }
        std::vector<InstanceData>        rasterInstances;
        const std::vector<InstanceData>& RasterInstances() const { return rasterInstances; }
        void                             RebuildRasterInstances()
        {
            const bool singleHybrid = mode == RenderMode::Hybrid && !scene.active && model && !model->vertices.empty();
            rasterInstances = scene.active ? scene.instances : std::vector<InstanceData>{};
            if (singleHybrid)
            {
                if (singleBVHModel != model)
                {
                    singleBVHModel = model;
                    singleBVH      = std::make_shared<PickMesh>(model);
                }
                InstanceData identity{};
                for (int k : {0, 5, 10, 15})
                    identity.world[k] = identity.normal[k] = 1;
                identity.uv[0] = 1;
                identity.uv[2] = 1;
                identity.uv[3] = float(singleBVH->ShadowNodeCount());
                rasterInstances.push_back(identity);
            }
            if (model)
                for (const auto& record : model->materialRecords)
                {
                    InstanceData v;
                    std::memcpy(&v, record.data(), sizeof(v));
                    rasterInstances.push_back(v);
                }
            rasterGlobals = uint32_t(rasterInstances.size());
            InstanceData globals{};
            std::copy(fogSettings.color.begin(), fogSettings.color.end(), globals.baseColor);
            globals.baseColor[3] = float(fogSettings.mode);
            globals.material[0]  = fogSettings.nearDistance;
            globals.material[1]  = fogSettings.farDistance;
            globals.material[2]  = fogSettings.density;
            globals.emissive[0]  = scene.active ? float(scene.instances.size()) : singleHybrid ? 1.f : 0;
            globals.emissive[1]  = shadowSettings.enabled ? 1.f : 0.f;
            globals.emissive[2]  = shadowSettings.softness;
            globals.emissive[3]  = float(shadowSettings.samples);
            globals.uv[0]        = shadowSettings.bias;
            globals.world[0]     = hybrid.ambientOcclusion ? hybrid.aoRadius : 0;
            globals.world[1]     = float(hybrid.aoSamples);
            globals.world[2]     = hybrid.aoStrength;
            globals.world[3]     = float(hybrid.view);
            globals.world[4]     = hybrid.reflections ? hybrid.reflectionDistance : 0;
            globals.world[5]     = float(hybrid.reflectionSamples);
            globals.world[6]     = hybrid.reflectionStrength;
            rasterInstances.push_back(globals);
            if (scene.active)
                rasterInstances.insert(rasterInstances.end(), scene.shadowRecords.begin(), scene.shadowRecords.end());
            if (singleHybrid)
                for (const auto& row : singleBVH->ShadowRecords())
                {
                    InstanceData record;
                    std::memcpy(&record, row.data(), 192);
                    rasterInstances.push_back(record);
                }
            rasterInstances[rasterGlobals].uv[2] = float(rasterInstances.size());
            rasterInstances[rasterGlobals].uv[3] = lightCollectionActive ? 1.f : 0.f;
            unsigned count                       = 0;
            for (const auto& light : lights)
                if (light.enabled)
                {
                    InstanceData record{};
                    for (unsigned k = 0; k < 3; ++k)
                    {
                        record.baseColor[k] = light.color[k];
                        record.material[k]  = light.position[k];
                        record.emissive[k]  = light.direction[k];
                    }
                    record.baseColor[3] = light.intensity;
                    record.material[3]  = light.range;
                    record.emissive[3]  = float(light.type);
                    record.uv[0]        = std::cos(light.innerConeAngle);
                    record.uv[1]        = std::cos(light.outerConeAngle);
                    record.uv[2]        = light.castShadow ? 1.f : 0.f;
                    record.uv[3]        = light.shadowSoftness;
                    rasterInstances.push_back(record);
                    ++count;
                }
            rasterInstances[rasterGlobals].uv[1] = float(count);
        }
        SceneShadowSettings shadowSettings;
        void                SetShadows(const SceneShadowSettings& value)
        {
            if (
              !std::isfinite(value.softness) || value.softness < 0 || value.softness > 4 || value.samples < 1
              || value.samples > 16 || !std::isfinite(value.bias) || value.bias < .00001f || value.bias > .1f)
                throw std::invalid_argument("Invalid scene shadow settings");
            shadowSettings = value;
            RebuildRasterInstances();
            rasterAA.Reset();
        }
        SceneShadowSettings GetShadows() const { return shadowSettings; }
        FogSettings fogSettings;
        uint32_t    rasterGlobals = 0;
        void        SetFog(const FogSettings& value)
        {
            if (
              static_cast<unsigned>(value.mode) > 2 || !std::isfinite(value.nearDistance) || value.nearDistance < 0
              || !std::isfinite(value.farDistance) || value.farDistance <= value.nearDistance
              || !std::isfinite(value.density) || value.density < 0 || value.density > 1)
                throw std::invalid_argument("Invalid fog settings");
            for (float c : value.color)
                if (!std::isfinite(c) || c < 0 || c > 1)
                    throw std::invalid_argument("Invalid fog color");
            fogSettings = value;
            RebuildRasterInstances();
            rasterAA.Reset();
        }
        FogSettings GetFog() const { return fogSettings; }
        void ApplyRasterSettings(SceneUniforms& values) const { values.irradiance[3][3] = float(rasterGlobals + 1); }
        void ConfigureUI()
        {
#ifdef NOVA_HAS_UI
            if (parameterPanel)
                parameterPanel->ConfigureUI(assets.config.configureUI);
#endif
        }
        float MaterialMode() const
        {
            return scene.active || (mode == RenderMode::Hybrid && model && !model->vertices.empty()) ? 1.f
                   : model && !model->materialRecords.empty()                                        ? 2.f
                                                                                                     : 0.f;
        }
        void        ConfigurePathTracing(GraphicsAPI api, const RendererConfig& config)
        {
            pathAPI    = api;
            pathConfig = config;
        }
        RendererCapabilities GetCapabilities()
        {
            if (HasAlpha(model.get()))
                return {false, false, "", "MASK/BLEND ray tracing is not supported; use raster"};
            if (model && model->animation)
                return {false, false, "", "Animated models currently use raster rendering"};
            return pathSession.Capabilities(pathAPI, pathConfig);
        }
        AnimationPlayer animation;
        void            SetAnimationState(const AnimationState& state)
        {
            const auto clips = animation.Clips();
            if (
              state.clip >= clips.size() || !std::isfinite(state.time) || state.time < 0
              || state.time > clips[state.clip].duration || !std::isfinite(state.speed) || state.speed < .1f
              || state.speed > 4)
                throw std::invalid_argument("Invalid animation state");
            if (state.clip != animation.state.clip || std::abs(state.time - animation.state.time) > .25f)
                rasterAA.Reset();
            animation.state = state;
            animation.Apply();
        }
        void SetRenderMode(RenderMode value)
        {
            if (value != RenderMode::Raster && value != RenderMode::RayTracing && value != RenderMode::Hybrid)
                throw std::invalid_argument("Invalid render mode");
            if (value == RenderMode::Hybrid)
            {
                if (!scene.active && (!model || model->vertices.empty()))
                    throw std::invalid_argument("Hybrid requires a model or scene");
                auto caps = GetHybridCapabilities();
                if (!caps.supported)
                    throw std::invalid_argument(caps.reason);
                if (
                  rasterSettings.method == AAMethod::MSAA || rasterSettings.method == AAMethod::TAA
                  || rasterSettings.split)
                    throw std::invalid_argument("Hybrid supports Off/FXAA/SMAA/SSAA without AA split");
            }
            if (value == RenderMode::RayTracing)
            {
                if (!scene.active && (!model || model->vertices.empty()))
                    throw std::invalid_argument("Load a model before path tracing");
                const auto caps = GetCapabilities();
                if (!caps.pathTracing)
                    throw std::runtime_error(caps.reason);
            }
            if (mode != value)
            {
                mode = value;
                rasterAA.gpuPresentation = mode == RenderMode::Hybrid;
                if (mode == RenderMode::Hybrid)
                    modelRaster.pipeline = RasterPipeline::Deferred;
                RebuildRasterInstances();
                rasterAA.Reset();
                pathSession.Reset();
            }
        }
        RenderMode GetRenderMode() const { return mode; }
        void       SetPostProcessing(const PostProcessingSettings& settings)
        {
            ValidatePostProcessing(settings);
            if (settings.enabled != postSettings.enabled || settings.autoExposure != postSettings.autoExposure)
                rasterAA.postState.Reset();
            if (settings.enabled != postSettings.enabled)
                rasterAA.Reset();
            postSettings = settings;
            if (!model && settings.enabled)
            {
                LoadModel(EmptyModelData());
                aaTriangle = true;
            }
        }
        PostProcessingSettings GetPostProcessing() const { return postSettings; }
        RasterSettings         modelRaster;
        RasterCapabilities     GetRasterCapabilities() const { return {}; }
        RasterSettings         GetRasterSettings() const { return modelRaster; }
        void                   SetRasterSettings(const RasterSettings& value)
        {
            if (mode == RenderMode::Hybrid && value.pipeline != RasterPipeline::Deferred)
                throw std::invalid_argument("Hybrid requires Deferred raster visibility");
            if (
              unsigned(value.pipeline) > 1 || unsigned(value.view) > 9 || !std::isfinite(value.debugRange)
              || value.debugRange <= 0 || value.debugRange > 1000000)
                throw std::invalid_argument("Invalid model raster settings; only Forward and Deferred are supported");
            if (value.pipeline == RasterPipeline::Forward && value.view != GBufferView::Final)
                throw std::invalid_argument("G-buffer views require Deferred");
            if (value.pipeline == RasterPipeline::Deferred && rasterSettings.method == AAMethod::MSAA)
                throw std::invalid_argument("Deferred MSAA requires per-sample lighting and is not supported");
            modelRaster = value;
            rasterAA.Reset();
        }
        void                   SetAntiAliasing(const AntiAliasingSettings& value)
        {
            if (
              mode == RenderMode::Hybrid
              && (value.method == AAMethod::MSAA || value.method == AAMethod::TAA || value.split))
                throw std::invalid_argument("Hybrid supports Off/FXAA/SMAA/SSAA without AA split");
            if (
              HasBlend(model.get()) && (value.method == AAMethod::MSAA || value.method == AAMethod::TAA || value.split))
                throw std::invalid_argument("BLEND supports Off/FXAA/SMAA/SSAA without AA split");
            if (modelRaster.pipeline == RasterPipeline::Deferred && value.method == AAMethod::MSAA)
                throw std::invalid_argument("Deferred MSAA is not supported");
            if (
              static_cast<unsigned>(value.method) > 5 || !std::isfinite(value.historyWeight) || value.historyWeight < 0
              || value.historyWeight > .98f || !std::isfinite(value.zoom) || value.zoom < 1 || value.zoom > 8)
                throw std::invalid_argument("Invalid AA settings");
            if (
              rasterSettings.method != value.method || rasterSettings.historyWeight != value.historyWeight
              || rasterSettings.zoom != value.zoom || rasterSettings.split != value.split)
                rasterAA.Reset();
            rasterSettings = value;
            if (!model && value.method != AAMethod::Off)
            {
                LoadModel(EmptyModelData());
                aaTriangle = true;
            }
        }
        AntiAliasingSettings GetAntiAliasing() const { return rasterSettings; }
        const Image&         GetRasterAAImage() const { return rasterAA.image; }
        void                 EnableGPUFramePresentation(bool enabled) { rasterAA.gpuPresentation = enabled; }
        uint64_t RasterReadbackCount() const { return rasterAA.device ? rasterAA.device->readbackCount : 0; }
        void*    RasterGPUOutput() const { return rasterAA.device ? rasterAA.device->GPUOutput() : nullptr; }
        Image    CaptureRasterFrame()
        {
            if (!rasterAA.device)
                throw std::runtime_error("Draw before capture");
            return rasterAA.device->Capture();
        }
        uint32_t             GetAntiAliasingHistoryFrames() const
        {
            return rasterSettings.method == AAMethod::TAA && modelRaster.view == GBufferView::Final ? rasterAA.frames
                                                                                                    : 0;
        }

        void SetPathTracing(const PathTracingSettings& value)
        {
            ValidatePathTracing(value);
            if (value.integrator == PathIntegrator::Metropolis && !GetCapabilities().metropolis)
                throw std::runtime_error("MLT requires a supported GPU; DX12 WARP is not supported");
            pathSettings = value;
            pathSession.Reset();
        }
        PathTracingSettings GetPathTracing() const { return pathSettings; }
        void                ResetAccumulation() { pathSession.Reset(); }
        uint32_t            GetAccumulatedSamples() const { return pathSession.Samples(); }
        void                PresentImage(const Image& image)
        {
            if (
              !image.width || !image.height || image.width > 8192 || image.height > 8192
              || image.pixels.size() != size_t(image.width) * image.height * 4)
                throw std::invalid_argument("Image requires width*height RGBA8 pixels, dimensions 1..8192");
            if (!model)
                LoadModel(EmptyModelData());
            presentedImage = image;
        }
        void ClearImage() { presentedImage.reset(); }
        virtual ~Backend()                                          = default;
        virtual void        Resize(uint32_t width, uint32_t height) = 0;
        virtual FrameResult DrawFrame()                             = 0;
        virtual void        WaitIdle()                              = 0;
        virtual void        LoadModel(std::shared_ptr<const ModelData> data)
        {
            ValidateMaterialState(data.get());
            scene.active = false;
            if (data != model)
            {
                rasterAA.Reset();
                aaTriangle = false;
                animation.Load(std::move(data));
                model = animation.Model();
            }
            RebuildRasterInstances();
            if (model && model->animation)
                SetRenderMode(RenderMode::Raster);
        }
        virtual void LoadEnvironment(std::shared_ptr<const EnvironmentData> data)
        {
            rasterAA.Reset();
            environment = std::move(data);
            const bool sceneWasActive = scene.active;
            LoadModel(model ? model : EmptyModelData());
            scene.active = sceneWasActive;
            RebuildRasterInstances();
        }
        void SetEnvironment(const EnvironmentSettings& value)
        {
            if (std::memcmp(&environmentSettings, &value, sizeof(value)) != 0)
                rasterAA.Reset();
            environmentSettings = value;
        }
        EnvironmentSettings GetEnvironment() const { return environmentSettings; }
        bool                EnableParameterPanel(GLFWwindow* window, bool enabled)
        {
#ifdef NOVA_HAS_UI
            if (enabled && !parameterPanel)
                parameterPanel = std::make_unique<ParameterPanel>(window);
            if (enabled)
                parameterPanel->CaptureDefaults(camera, directionalLight, environmentSettings);
            if (parameterPanel)
            {
                parameterPanel->Activate();
                auto& io = ImGui::GetIO();
                io.SetAppAcceptingEvents(enabled);
                if (!enabled)
                {
                    io.ClearEventsQueue();
                    io.ClearInputKeys();
                    io.ClearInputMouse();
                }
            }
            panelEnabled = enabled;
            return true;
#else
            (void)window;
            return !enabled;
#endif
        }
        bool UIWantsMouse() const
        {
#ifdef NOVA_HAS_UI
            return panelEnabled && parameterPanel && parameterPanel->WantsMouse();
#else
            return false;
#endif
        }
        bool UIWantsKeyboard() const
        {
#ifdef NOVA_HAS_UI
            return panelEnabled && parameterPanel && parameterPanel->WantsKeyboard();
#else
            return false;
#endif
        }
        std::array<float, 4> GetClearColor() const { return clearColor; }
        void                 SetClearColor(const std::array<float, 4>& value)
        {
            if (clearColor != value)
                rasterAA.Reset();
            clearColor = value;
        }
        OrbitCamera GetCamera() const { return camera; }
        Camera      GetViewCamera() const { return viewCamera ? *viewCamera : CameraFromOrbit(camera); }
        void        SetViewCamera(const Camera& value)
        {
            const auto next = ValidateViewCamera(value);
            if (viewCamera && SameCamera(*viewCamera, next))
                return;
            viewCamera = next;
            rasterAA.Reset();
            pathSession.Reset();
        }
        void SetViewport(const std::optional<Viewport>& value)
        {
            ValidateViewport(value);
            if (SameViewport(viewport, value))
                return;
            viewport = value;
            rasterAA.Reset();
            pathSession.Reset();
        }
        std::optional<Viewport> GetViewport() const { return viewport; }
        Viewport                SceneViewport(uint32_t width, uint32_t height) const
        {
#ifdef NOVA_HAS_UI
            if (!viewport && panelEnabled && parameterPanel && assets.config.sceneViewport)
            {
                const auto rect  = assets.config.sceneViewport();
                const auto scale = ImGui::GetIO().DisplayFramebufferScale;
                for (float v : rect)
                    if (!std::isfinite(v))
                        throw std::invalid_argument("UI scene viewport must be finite");
                auto pixel = [](float v, float scale) {
                    return uint32_t(std::clamp(std::round(double(v) * scale), 0.0, double(UINT32_MAX)));
                };
                return ResolveViewport(
                  Viewport{
                    pixel(rect[0], scale.x), pixel(rect[1], scale.y), pixel(rect[2], scale.x), pixel(rect[3], scale.y)},
                  width,
                  height);
            }
#endif
            return ResolveViewport(viewport, width, height, SceneTopInset(height));
        }

        void        SetDirectionalLight(const DirectionalLight& value)
        {
            if (lightCollectionActive || std::memcmp(&directionalLight, &value, sizeof(value)) != 0)
                rasterAA.Reset();
            directionalLight = value;
            lightCollectionActive = false;
            lights.clear();
            RebuildRasterInstances();
            pathSession.Reset();
        }
        DirectionalLight GetDirectionalLight() const { return directionalLight; }
        void             SetLighting(LightingMode value)
        {
            if (lighting != value)
                rasterAA.Reset();
            lighting = value;
        }
        void SetCamera(const OrbitCamera& value)
        {
            if (viewCamera)
                rasterAA.Reset();
            viewCamera.reset();
            if (
              std::abs(value.yaw - camera.yaw) > 20 || std::abs(value.pitch - camera.pitch) > 20
              || std::abs(value.distance - camera.distance) > 2 || value.exposure != camera.exposure
              || value.target != camera.target)
                rasterAA.Reset();
            camera = value;
        }

      protected:
        uint32_t SceneTopInset(uint32_t height) const
        {
#ifdef NOVA_HAS_UI
            if (panelEnabled && parameterPanel)
            {
                const auto& io = ImGui::GetIO();
                const float inset =
                  std::isfinite(assets.config.sceneTopInset) ? std::clamp(assets.config.sceneTopInset, 0.f, 256.f) : 0;
                const float pixels = inset * io.DisplayFramebufferScale.y;
                return std::min(height > 0 ? height - 1 : 0, uint32_t(pixels));
            }
#endif
            return 0;
        }
        AntiAliasingSettings   rasterSettings;
        PostProcessingSettings postSettings;
        std::shared_ptr<const ModelData> singleBVHModel;
        std::shared_ptr<PickMesh>        singleBVH;
        detail::RasterAA       rasterAA;
        bool                   aaTriangle = false;
        std::optional<Image>   presentedImage;
        RenderMode             mode = RenderMode::Raster;
        PathTracingSettings    pathSettings;
        PathSession            pathSession;
        GraphicsAPI            pathAPI = GraphicsAPI::Auto;
        RendererConfig         pathConfig;
        const ModelImage*      DrawPathFrame(uint32_t width, uint32_t height)
        {
            if (presentedImage)
                return &*presentedImage;
            if (mode != RenderMode::RayTracing)
            {
                if (
                  rasterSettings.method == AAMethod::Off && !aaTriangle && !postSettings.enabled
                  && modelRaster.pipeline == RasterPipeline::Forward && !HasBlend(model.get()))
                    return nullptr;
                if (modelRaster.pipeline == RasterPipeline::Deferred && (!model || aaTriangle))
                    throw std::runtime_error("Deferred requires a model or scene");
                if (!rasterAA.device)
                    CreateRasterAADevice();
                rasterAA.current = MakeSceneUniforms(
                  *model,
                  GetViewCamera(),
                  float(width) / height,
                  lighting,
                  directionalLight,
                  environment.get(),
                  environmentSettings);
                rasterAA.current.lightDirection[3] = MaterialMode();
                ApplyRasterSettings(rasterAA.current);
                rasterAA.current.irradiance[1][3] = mode == RenderMode::Hybrid ? 1.f : 0.f;
                rasterAA.current.irradiance[2][3] = float(hybrid.shadowSamples);
                rasterAA.current.irradiance[4][3] = hybrid.shadows ? 1.f : 0.f;
                rasterAA.current.irradiance[5][3] =
                  hybrid.view == HybridView::Shadows ? float(hybrid.shadowLight + 1) : 0.f;
                const bool debug                  = modelRaster.view != GBufferView::Final;
                rasterAA.current.irradiance[0][3] =
                  !debug && !(mode == RenderMode::Hybrid && hybrid.view != HybridView::Final) && postSettings.enabled
                    ? 1.f
                    : 0.f;
                rasterAA.current.irradiance[7][3] = modelRaster.debugRange;
                rasterAA.current.irradiance[8][3] = float(modelRaster.view);
                const auto& image                 = rasterAA.Render(
                  width,
                  height,
                  rasterSettings,
                  ClearColorForTarget(true),
                  aaTriangle,
                  postSettings,
                  modelRaster,
                  HasBlend(model.get()),
                  mode == RenderMode::Hybrid,
                  hybrid.view != HybridView::Final);
                rasterAA.previousJoints = model->jointMatrices;
                return &image;
            }
            const auto uniform = MakeSceneUniforms(
              *model,
              GetViewCamera(),
              float(width) / height,
              LightingMode::PBR,
              directionalLight,
              environment.get(),
              environmentSettings);
            auto rayModel = scene.active ? scene.RayModel() : model;
            return &pathSession.Render(
              rayModel,
              environment,
              uniform,
              pathSettings,
              ClearColorForTarget(true),
              width,
              height,
              nullptr,
              lightCollectionActive ? &lights : nullptr,
              shadowSettings);
        }

        virtual void         CreateRasterAADevice() { throw std::runtime_error("Raster AA backend not implemented"); }
        std::array<float, 4> ClearColorForTarget(bool srgb) const
        {
            auto result = clearColor;
            if (srgb)
                for (int k = 0; k < 3; ++k)
                    result[k] = SRGBToLinear(result[k]);
            return result;
        }
        std::array<float, 4> clearColor{0.025f, 0.035f, 0.055f, 1};
#ifdef NOVA_HAS_UI
        std::unique_ptr<ParameterPanel> parameterPanel;
        bool                            panelEnabled = false;
        void                            DrawParameterPanel()
        {
            const auto previous       = animation.state;
            const auto previousAA     = rasterSettings;
            const auto previousRaster = modelRaster;
            const auto previousHybrid = hybrid;
            const auto oldClear       = clearColor;
            const auto oldCamera      = camera;
            const auto oldLight       = directionalLight;
            const auto oldEnvironment = environmentSettings;
            parameterPanel->Draw(
              bool(model) && !aaTriangle,
              lighting,
              camera,
              directionalLight,
              clearColor,
              bool(environment),
              environmentSettings,
              assets,
              mode,
              model && !model->vertices.empty() ? GetCapabilities() : RendererCapabilities{},
              pathSettings,
              pathSession.Samples(),
              animation.Clips(),
              animation.state,
              rasterSettings,
              modelRaster,
              hybrid,
              GetHybridCapabilities());
            if (
              previousHybrid.shadows != hybrid.shadows || previousHybrid.shadowSamples != hybrid.shadowSamples
              || previousHybrid.view != hybrid.view || previousHybrid.shadowLight != hybrid.shadowLight
              || previousHybrid.ambientOcclusion != hybrid.ambientOcclusion
              || previousHybrid.aoRadius != hybrid.aoRadius || previousHybrid.aoStrength != hybrid.aoStrength
              || previousHybrid.aoSamples != hybrid.aoSamples || previousHybrid.reflections != hybrid.reflections
              || previousHybrid.reflectionDistance != hybrid.reflectionDistance
              || previousHybrid.reflectionStrength != hybrid.reflectionStrength
              || previousHybrid.reflectionSamples != hybrid.reflectionSamples)
            {
                RebuildRasterInstances();
                rasterAA.Reset();
            }
            if (
              oldCamera.yaw != camera.yaw || oldCamera.pitch != camera.pitch || oldCamera.distance != camera.distance
              || oldCamera.target != camera.target || oldCamera.exposure != camera.exposure)
            {
                viewCamera.reset();
                rasterAA.Reset();
                pathSession.Reset();
            }
            if (lightCollectionActive)
                directionalLight = oldLight;
            if (
              previousRaster.pipeline != modelRaster.pipeline || previousRaster.view != modelRaster.view
              || previousRaster.debugRange != modelRaster.debugRange)
                rasterAA.Reset();
            const auto nextAA = rasterSettings;
            rasterSettings    = previousAA;
            if (nextAA.method != previousAA.method)
                SetAntiAliasing(nextAA);
            if (
              oldClear != clearColor || oldCamera.exposure != camera.exposure
              || std::abs(oldCamera.yaw - camera.yaw) > 20 || std::abs(oldCamera.pitch - camera.pitch) > 20
              || std::abs(oldCamera.distance - camera.distance) > 2 || oldLight.direction != directionalLight.direction
              || oldLight.color != directionalLight.color || oldLight.intensity != directionalLight.intensity
              || oldEnvironment.rotation != environmentSettings.rotation
              || oldEnvironment.intensity != environmentSettings.intensity
              || oldEnvironment.backgroundIntensity != environmentSettings.backgroundIntensity
              || oldEnvironment.showBackground != environmentSettings.showBackground)
                rasterAA.Reset();
            if (previous.clip != animation.state.clip || std::abs(previous.time - animation.state.time) > .25f)
                rasterAA.Reset();
            if (previous.clip != animation.state.clip || previous.time != animation.state.time)
                animation.Apply();
        }
#endif
        std::shared_ptr<const EnvironmentData> environment;
        EnvironmentSettings                    environmentSettings;
        std::shared_ptr<const ModelData>       model;
        OrbitCamera                            camera;
        std::optional<Camera>                  viewCamera;
        std::optional<Viewport>                viewport;
        DirectionalLight                       directionalLight;
        LightingMode                           lighting = LightingMode::Unlit;

      public:
        virtual uint32_t ValidationErrorCount() const = 0;
    };
    std::unique_ptr<Backend> CreateVulkanBackend(GLFWwindow*, const RendererConfig&);
#ifdef __APPLE__
    std::unique_ptr<Backend> CreateMetalBackend(GLFWwindow*, const RendererConfig&);
#endif
#ifdef _WIN32
    std::unique_ptr<Backend> CreateD3D12Backend(GLFWwindow*, const RendererConfig&);
#endif
} // namespace render::detail
