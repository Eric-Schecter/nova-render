#pragma once
#include "Color.hpp"
#include "ModelData.hpp"
#include "ParameterPanel.hpp"
#include "path/PathTracing.hpp"
#include "render/Renderer.hpp"
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
        RendererCapabilities GetCapabilities() { return pathSession.Capabilities(pathAPI, pathConfig); }
        void                 SetRenderMode(RenderMode value)
        {
            if (value != RenderMode::Raster && value != RenderMode::PathTracing)
                throw std::invalid_argument("Invalid render mode");
            if (value == RenderMode::PathTracing)
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
        PathTracingSettings GetPathTracing() const { return pathSettings; }
        void                ResetAccumulation() { pathSession.Reset(); }
        uint32_t            GetAccumulatedSamples() const { return pathSession.Samples(); }
        virtual ~Backend()                                          = default;
        virtual void        Resize(uint32_t width, uint32_t height) = 0;
        virtual FrameResult DrawFrame()                             = 0;
        virtual void        WaitIdle()                              = 0;
        virtual void        LoadModel(std::shared_ptr<const ModelData> data) { model = std::move(data); }
        virtual void        LoadEnvironment(std::shared_ptr<const EnvironmentData> data)
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
            if (mode != RenderMode::PathTracing)
                return nullptr;
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
              pathSession.Samples());
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
