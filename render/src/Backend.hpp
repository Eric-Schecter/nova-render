#pragma once
#include "AnimationData.hpp"
#include "Color.hpp"
#include "ModelData.hpp"
#include "ParameterPanel.hpp"
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
        void        ConfigurePathTracing(GraphicsAPI api, const RendererConfig& config)
        {
            pathAPI    = api;
            pathConfig = config;
        }
        RendererCapabilities GetCapabilities()
        {
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
            animation.state = state;
            animation.Apply();
        }
        void SetRenderMode(RenderMode value)
        {
            if (
              value != RenderMode::Raster && value != RenderMode::PathTracing
              && value != RenderMode::IntegratorComparison)
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
                pathSession.Reset();
            }
        }
        RenderMode GetRenderMode() const { return mode; }
        void       SetPathTracing(const PathTracingSettings& value)
        {
            ValidatePathTracing(value);
            pathSettings = value;
            pathSession.Reset();
        }
        PathTracingSettings               GetPathTracing() const { return pathSettings; }
        void                              ResetAccumulation() { pathSession.Reset(); }
        uint32_t                          GetAccumulatedSamples() const { return pathSession.Samples(); }
        std::array<ComparisonProgress, 4> GetComparisonProgress() const { return pathSession.Comparison(); }
        virtual ~Backend()                                          = default;
        virtual void        Resize(uint32_t width, uint32_t height) = 0;
        virtual FrameResult DrawFrame()                             = 0;
        virtual void        WaitIdle()                              = 0;
        virtual void        LoadModel(std::shared_ptr<const ModelData> data)
        {
            if (data != model)
            {
                animation.Load(std::move(data));
                model = animation.Model();
            }
            if (model && model->animation)
                SetRenderMode(RenderMode::Raster);
        }
        virtual void LoadEnvironment(std::shared_ptr<const EnvironmentData> data)
        {
            environment = std::move(data);
            LoadModel(model ? model : EmptyModelData());
        }
        void                SetEnvironment(const EnvironmentSettings& value) { environmentSettings = value; }
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
        void                 SetClearColor(const std::array<float, 4>& value) { clearColor = value; }
        OrbitCamera          GetCamera() const { return camera; }
        void                 SetDirectionalLight(const DirectionalLight& value) { directionalLight = value; }
        DirectionalLight     GetDirectionalLight() const { return directionalLight; }
        void                 SetLighting(LightingMode value) { lighting = value; }
        void                 SetCamera(const OrbitCamera& value) { camera = value; }

      protected:
        RenderMode          mode = RenderMode::Raster;
        PathTracingSettings pathSettings;
        PathSession         pathSession;
        GraphicsAPI         pathAPI = GraphicsAPI::Auto;
        RendererConfig      pathConfig;
        const ModelImage*   DrawPathFrame(uint32_t width, uint32_t height)
        {
            if (mode == RenderMode::Raster)
                return nullptr;
            const auto uniform = MakeSceneUniforms(
              *model,
              camera,
              float(width) / height,
              LightingMode::PBR,
              directionalLight,
              environment.get(),
              environmentSettings);
            if (mode == RenderMode::IntegratorComparison)
                return &pathSession.RenderComparison(model, uniform, pathSettings, width, height);
            return &pathSession.Render(
              model, environment, uniform, pathSettings, ClearColorForTarget(true), width, height);
        }

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
            const auto previous = animation.state;
            parameterPanel->Draw(
              bool(model),
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
              pathSession.Comparison(),
              animation.Clips(),
              animation.state);
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
