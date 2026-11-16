#pragma once
#include "AnimationData.hpp"
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
        void        SetScene(const std::vector<SceneAsset>& assets, const std::vector<SceneInstance>& instances)
        {
            auto next = scene;
            next.Update(assets, instances);
            if (model != next.model)
                LoadModel(next.model);
            scene      = std::move(next);
            aaTriangle = false;
            presentedImage.reset();
            pathSession.Reset();
            rasterAA.Reset();
            mode = RenderMode::Raster;
        }
        void        ConfigurePathTracing(GraphicsAPI api, const RendererConfig& config)
        {
            pathAPI    = api;
            pathConfig = config;
        }
        RendererCapabilities GetCapabilities()
        {
            if (scene.active)
                return {false, false, "", "Scene instances currently use raster rendering"};
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
            if (value != RenderMode::Raster && value != RenderMode::PathTracing)
                throw std::invalid_argument("Invalid render mode");
            if (value != RenderMode::Raster)
            {
                if (!model || model->vertices.empty())
                    throw std::invalid_argument("Load a model before path tracing");
                const auto caps = GetCapabilities();
                if (!caps.pathTracing)
                    throw std::runtime_error(caps.reason);
            }
            if (mode != value)
            {
                mode = value;
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
        void                   SetAntiAliasing(const AntiAliasingSettings& value)
        {
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
        uint32_t             GetAntiAliasingHistoryFrames() const
        {
            return rasterSettings.method == AAMethod::TAA ? rasterAA.frames : 0;
        }

        void SetPathTracing(const PathTracingSettings& value)
        {
            ValidatePathTracing(value);
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
            scene.active = false;
            if (data != model)
            {
                rasterAA.Reset();
                aaTriangle = false;
                animation.Load(std::move(data));
                model = animation.Model();
            }
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
        void        SetDirectionalLight(const DirectionalLight& value)
        {
            if (std::memcmp(&directionalLight, &value, sizeof(value)) != 0)
                rasterAA.Reset();
            directionalLight = value;
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
            if (mode == RenderMode::Raster)
            {
                if (rasterSettings.method == AAMethod::Off && !aaTriangle && !postSettings.enabled)
                    return nullptr;
                if (!rasterAA.device)
                    CreateRasterAADevice();
                rasterAA.current = MakeSceneUniforms(
                  *model,
                  camera,
                  float(width) / height,
                  lighting,
                  directionalLight,
                  environment.get(),
                  environmentSettings);
                rasterAA.current.lightDirection[3] = scene.active ? 1.f : 0.f;
                rasterAA.current.irradiance[0][3] = postSettings.enabled ? 1.f : 0.f;
                const auto& image =
                  rasterAA.Render(width, height, rasterSettings, ClearColorForTarget(true), aaTriangle, postSettings);
                rasterAA.previousJoints = model->jointMatrices;
                return &image;
            }
            const auto uniform = MakeSceneUniforms(
              *model,
              camera,
              float(width) / height,
              LightingMode::PBR,
              directionalLight,
              environment.get(),
              environmentSettings);
            return &pathSession.Render(
              model, environment, uniform, pathSettings, ClearColorForTarget(true), width, height);
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
              rasterSettings);
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
