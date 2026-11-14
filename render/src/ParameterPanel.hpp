#pragma once
#include "render/Animation.hpp"
#include "render/Model.hpp"
#include "render/ParameterPanel.hpp"
#include "render/RenderMode.hpp"
#include <array>
#include <optional>
namespace render::detail
{
    struct PanelAssets
    {
        ParameterPanelConfig                 config;
        std::filesystem::path                modelPath, environmentPath;
        std::optional<std::filesystem::path> requestedModel, requestedEnvironment;
        std::optional<RenderMode>            requestedMode;
        bool                                 requestedReset = false;
        std::string                          error;
    };
} // namespace render::detail
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
          bool                              model,
          LightingMode                      lighting,
          OrbitCamera&                      camera,
          DirectionalLight&                 light,
          std::array<float, 4>&             clear,
          bool                              hasEnvironment,
          EnvironmentSettings&              environment,
          PanelAssets&                      assets,
          RenderMode                        mode,
          const RendererCapabilities&       capabilities,
          PathTracingSettings&              path,
          uint32_t                          samples,
          const std::vector<AnimationClip>& clips,
          AnimationState&                   animation);
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
