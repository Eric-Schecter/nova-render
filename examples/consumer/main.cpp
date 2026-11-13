#include <iostream>
#include <render/Window.hpp>
#include <stdexcept>

int main(int argc, char** argv)
{
    try
    {
        render::WindowConfig config;
        config.title = "Nova installed consumer";
        if (argc > 1)
        {
            const std::string api = argv[1];
            if (api == "vulkan")
                config.renderer.api = render::GraphicsAPI::Vulkan;
            else if (api == "metal")
                config.renderer.api = render::GraphicsAPI::Metal;
            else if (api == "dx12")
                config.renderer.api = render::GraphicsAPI::D3D12;
            else
                throw std::runtime_error("Usage: nova_consumer [vulkan|metal|dx12]");
        }
        render::Window window(config);
        window.EnableParameterPanel();
        int frames = 0;
        while (frames < 120 && window.PollEvents())
        {
            if (window.DrawFrame() == render::FrameResult::Rendered)
                ++frames;
        }
        window.WaitIdle();
        std::cout << "Rendered " << frames << " frames; validation errors: " << window.ValidationErrorCount() << '\n';
        return frames == 120 && window.ValidationErrorCount() == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
