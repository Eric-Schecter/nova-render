#include "../common/PerformancePanel.hpp"
#include "render/Window.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

static void resizeWindow(render::Window& window, uint32_t width, uint32_t height)
{
    const auto previous = window.GetFramebufferSize();
    window.SetSize(width, height);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (window.GetFramebufferSize() == previous)
    {
        if (!window.PollEvents() || std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Window did not resize");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main(int argc, char** argv)
{
    try
    {
        render::GraphicsAPI api = render::GraphicsAPI::Auto;
        std::string         shaderDirectory; // Empty selects the library's embedded shaders.
        int                 frameLimit = 0;
        bool                resizeTest = false, warp = false;
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--frames" && i + 1 < argc)
            {
                const std::string value    = argv[++i];
                size_t            consumed = 0;
                frameLimit                 = std::stoi(value, &consumed);
                if (frameLimit <= 0 || consumed != value.size())
                    throw std::runtime_error("--frames must be a positive integer");
            }
            else if (arg == "--api" && i + 1 < argc)
            {
                const std::string name = argv[++i];
                if (name == "vulkan")
                    api = render::GraphicsAPI::Vulkan;
                else if (name == "metal")
                    api = render::GraphicsAPI::Metal;
                else if (name == "dx12")
                    api = render::GraphicsAPI::D3D12;
                else if (name == "auto")
                    api = render::GraphicsAPI::Auto;
                else
                    throw std::runtime_error("Unknown API: " + name);
            }
            else if (arg == "--shader-dir" && i + 1 < argc)
                shaderDirectory = argv[++i];
            else if (arg == "--warp")
                warp = true;
            else if (arg == "--resize-test")
                resizeTest = true;
            else
                throw std::runtime_error(
                  "Usage: nova_triangle [--api auto|vulkan|metal|dx12] [--warp] [--shader-dir PATH] [--frames N] "
                  "[--resize-test]");
        }
        render::RendererConfig config;
        config.api             = api;
        config.useWarp         = warp;
        config.shaderDirectory = shaderDirectory;
#ifdef NDEBUG
        config.enableValidation = false;
#endif
        render::ResolveGraphicsAPI(config); // Reject unsupported requests before creating a window.
        render::WindowConfig windowConfig;
        windowConfig.renderer = config;
        windowConfig.title = std::string("Nova Render | ") + render::GraphicsAPIName(render::ResolveGraphicsAPI(config))
                             + " Triangle" + (warp ? " (WARP)" : "");
        render::Window               window(windowConfig);
        example::PerformancePanel    performance;
        render::ParameterPanelConfig panel;
        performance.Attach(panel);
        window.EnableParameterPanel(panel);
        int frames = 0;
        while ((!frameLimit || frames < frameLimit) && window.PollEvents())
        {
            if (performance.DrawFrame(window, window.GetFramebufferSize()) != render::FrameResult::Rendered)
                continue;
            ++frames;
            if (resizeTest && frames == 20)
                resizeWindow(window, 1024, 640);
            if (resizeTest && frames == 40)
            {
                resizeWindow(window, windowConfig.width, windowConfig.height);
                std::cout << "Window resize check passed\n";
            }
        }
        window.WaitIdle();
        std::cout << "Rendered " << frames << " frames; validation errors: " << window.ValidationErrorCount() << '\n';
        return window.ValidationErrorCount() || (frameLimit && frames != frameLimit) ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
