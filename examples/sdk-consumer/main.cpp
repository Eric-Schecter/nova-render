#include <iostream>
#include <render/ReflectionProbeView.hpp>
#include <render/Window.hpp>
#include <stdexcept>
int main(int argc, char** argv)
{
    try
    {
        render::RendererConfig renderer;
        const std::string      api = argc > 1 ? argv[1] : "metal";
        renderer.api               = api == "metal"    ? render::GraphicsAPI::Metal
                                     : api == "vulkan" ? render::GraphicsAPI::Vulkan
                                                       : render::GraphicsAPI::D3D12;
        renderer.useWarp           = api == "dx12";
        render::ReflectionProbeView   view(renderer);
        render::ReflectionProbeVolume left, right;
        left.position         = {-2, .6f, 0};
        right.position        = {2, .6f, 0};
        left.influenceMax[0]  = 1;
        right.influenceMin[0] = -1;
        view.SetReflectionProbes({left, right});
        const auto& image = view.DrawFrame(128, 96);
        if (image.pixels.size() != 128 * 96 * 4 || view.GetProbeCaptureCount() != 2)
            throw std::runtime_error("Installed probe API failed");
        render::WindowConfig config;
        config.renderer = renderer;
        config.title    = "Installed Nova SDK consumer";
        render::Window window(config);
        window.PresentImage(image);
        int frames = 0;
        while (frames < 10 && window.PollEvents())
            if (window.DrawFrame() == render::FrameResult::Rendered)
                frames++;
        window.WaitIdle();
        if (frames != 10 || window.ValidationErrorCount())
            throw std::runtime_error("Installed window API failed");
        std::cout << "PASS installed SDK: two reflection probes, image readback, window; 10 frames\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
