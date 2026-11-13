#pragma once
#include "Color.hpp"
#include "ModelData.hpp"
#include "ParameterPanel.hpp"
#include "render/Renderer.hpp"

namespace render::detail
{
    class Backend
    {
      public:
        virtual ~Backend()                                          = default;
        virtual void        Resize(uint32_t width, uint32_t height) = 0;
        virtual FrameResult DrawFrame()                             = 0;
        virtual void        WaitIdle()                              = 0;
        virtual void        LoadModel(std::shared_ptr<const ModelData> data) { model = std::move(data); }
        bool                EnableParameterPanel(GLFWwindow* window, bool enabled)
        {
#ifdef NOVA_HAS_UI
            if (enabled && !parameterPanel)
                parameterPanel = std::make_unique<ParameterPanel>(window);
            if (enabled)
                parameterPanel->CaptureDefaults(camera, directionalLight);
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
        void DrawParameterPanel() { parameterPanel->Draw(bool(model), lighting, camera, directionalLight, clearColor); }
#endif
        std::shared_ptr<const ModelData> model;
        OrbitCamera                      camera;
        DirectionalLight                 directionalLight;
        LightingMode                     lighting = LightingMode::Unlit;

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
