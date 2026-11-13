#pragma once
#include "Renderer.hpp"
#include <string>

namespace render
{
    struct WindowConfig
    {
        uint32_t       width  = 800;
        uint32_t       height = 600;
        std::string    title  = "Nova";
        RendererConfig renderer;
    };

    // Convenience entry point: owns window, event system and renderer.
    // Main thread only; one live Window, without externally managed GLFW, at a time.
    class Window
    {
      public:
        explicit Window(const WindowConfig& config = {});
        ~Window();
        Window(const Window&)                     = delete;
        Window&          operator=(const Window&) = delete;
        bool             PollEvents(); // false after closing the window or pressing Escape.
        FrameResult      DrawFrame();  // Updates framebuffer size; yields briefly if skipped.
        void             WaitIdle();
        void             LoadModel(const std::filesystem::path& glbPath);
        void             SetDirectionalLight(const DirectionalLight& light);
        DirectionalLight GetDirectionalLight() const;
        void             SetLighting(LightingMode mode);
        // Returns false if UI was disabled at build time. Disabled by default.
        bool                 EnableParameterPanel(bool enabled = true);
        void                 SetClearColor(const std::array<float, 4>& color);
        std::array<float, 4> GetClearColor() const;
        void                 SetCamera(const OrbitCamera& camera);
        OrbitCamera          GetCamera() const;
        void                 EnableOrbitControls(bool enabled = true);
        GraphicsAPI          GetAPI() const;
        uint32_t             ValidationErrorCount() const;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace render
