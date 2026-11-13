#pragma once
#include "render/Model.hpp"
#include <array>
#ifdef NOVA_HAS_UI
#include <imgui.h>
struct GLFWwindow;
namespace render::detail
{
    // Owns only the context/input backend. GPU backends shut down before this context.
    class ParameterPanel
    {
      public:
        explicit ParameterPanel(GLFWwindow* window);
        ~ParameterPanel();
        void Activate() const;
        void SetSRGBOutput(bool enabled);
        void CaptureDefaults(
          const OrbitCamera& camera, const DirectionalLight& light, const EnvironmentSettings& environment);
        void Draw(
          bool                  model,
          LightingMode          lighting,
          OrbitCamera&          camera,
          DirectionalLight&     light,
          std::array<float, 4>& clear,
          bool                  hasEnvironment,
          EnvironmentSettings&  environment);
        bool WantsMouse() const;
        bool WantsKeyboard() const;

      private:
        GLFWwindow*                        window;
        ImGuiContext*                      context     = nullptr;
        bool                               initialized = false;
        OrbitCamera                        initialCamera;
        DirectionalLight                   initialLight;
        EnvironmentSettings                initialEnvironment;
        std::array<ImVec4, ImGuiCol_COUNT> displayColors;
        ImVec2                             position{}, size{};
    };
} // namespace render::detail
#endif
