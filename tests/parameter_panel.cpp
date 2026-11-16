#include "TestDevice.hpp"
#include <iostream>
#include <render/Window.hpp>
#include <stdexcept>
#include <string>
int main(int argc, char** argv)
{
    try
    {
        if (argc != 3)
            throw std::runtime_error("Expected API and GLB path");
        render::WindowConfig config;
        config.width          = 640;
        config.height         = 480;
        config.title          = "Nova | UI lifecycle validation";
        const std::string api = TestAPI(argv[1]);
        if (api == "metal")
            config.renderer.api = render::GraphicsAPI::Metal;
        else if (api == "vulkan")
            config.renderer.api = render::GraphicsAPI::Vulkan;
        else if (api == "dx12")
        {
            config.renderer.api     = render::GraphicsAPI::D3D12;
            config.renderer.useWarp = TestUseWarp(argv[1]);
        }
        else
            throw std::runtime_error("Unknown test API");
        render::Window window(config);
        auto           draw = [&] {
            int frames = 0;
            for (int attempt = 0; attempt < 200 && frames < 3 && window.PollEvents(); ++attempt)
                if (window.DrawFrame() == render::FrameResult::Rendered)
                    ++frames;
            if (frames != 3)
                throw std::runtime_error("UI frame did not render");
        };
        int                          configured = 0, applicationFrames = 0;
        render::ParameterPanelConfig application;
        application.configureUI      = [&] { configured++; };
        application.drawUI           = [&] { applicationFrames++; };
        application.showBuiltinPanel = false;
        application.sceneViewport    = [] { return std::array<float, 4>{10, 10, 300, 220}; };
        window.SetCloseOnEscape(false);
        window.EnableParameterPanel(application);
        window.EnableParameterPanel(application);
        if (configured != 1)
            throw std::runtime_error("Application UI configuration must run exactly once");
        window.EnableParameterPanel(false);
        if (!window.EnableParameterPanel())
            throw std::runtime_error("UI build unavailable");
        // Disable before GPU/font initialization, then draw without UI.
        window.EnableParameterPanel(false);
        draw();
        window.EnableParameterPanel();
        draw();
        // Repeated calls must preserve one input backend and one context.
        window.EnableParameterPanel();
        draw();
        window.EnableParameterPanel(false);
        draw();
        window.LoadModel(argv[2]);
        window.SetLighting(render::LightingMode::PBR);
        render::OrbitCamera camera;
        camera.yaw = 35;
        window.SetCamera(camera);
        window.EnableOrbitControls();
        window.EnableParameterPanel();
        draw();
        window.SetClearColor({0.8f, 0.85f, 0.95f, 1});
        draw();
        auto color = window.GetClearColor();
        color[0]   = 0;
        if (window.GetClearColor()[0] != 0.8f || window.GetCamera().yaw != 35)
            throw std::runtime_error("Panel changed API state unexpectedly");
        window.EnableParameterPanel(false);
        draw();
        if (applicationFrames == 0)
            throw std::runtime_error("Application UI did not draw");
        window.WaitIdle();
        if (window.ValidationErrorCount())
            throw std::runtime_error("GPU validation error");
        std::cout
          << "PASS UI enable/disable before first frame, repeated enable, model transition, API state and cleanup\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
