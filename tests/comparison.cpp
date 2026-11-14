#include <iostream>
#include <render/Window.hpp>
#include <stdexcept>
using namespace render;
int main(int argc, char** argv)
{
    try
    {
        if (argc != 3)
            throw std::runtime_error("Expected API and assets");
        WindowConfig config;
        config.width            = 900;
        config.height           = 400;
        std::string api         = argv[1];
        config.renderer.api     = api == "metal"    ? GraphicsAPI::Metal
                                  : api == "vulkan" ? GraphicsAPI::Vulkan
                                                    : GraphicsAPI::D3D12;
        config.renderer.useWarp = api == "dx12";
        Window window(config);
        if (!window.GetCapabilities().rayTracing)
            return 77;
        window.LoadModel(std::filesystem::path(argv[2]) / "CornellCaustics/CornellCaustics.glb");
        auto settings                = window.GetRayTracing();
        settings.maxBounces          = 8;
        settings.resolutionScale     = .25f;
        settings.photonsPerIteration = 256;
        settings.comparisonSamples   = 3;
        window.SetRayTracing(settings);
        window.SetRenderMode(RenderMode::IntegratorComparison);
        window.EnableParameterPanel();
        for (int i = 0; i < 5; ++i)
        {
            window.PollEvents();
            window.DrawFrame();
        }
        auto progress = window.GetComparisonProgress();
        for (auto& p : progress)
            if (p.samples != 3 || !p.complete)
                throw std::runtime_error("Sample budget did not freeze exactly");
        window.DrawFrame();
        auto frozen = window.GetComparisonProgress();
        for (int i = 0; i < 3; ++i)
            if (frozen[i].milliseconds != progress[i].milliseconds)
                throw std::runtime_error("Frozen comparison still rendering");
        auto camera = window.GetCamera();
        camera.yaw  = 5;
        window.SetCamera(camera);
        window.DrawFrame();
        for (auto& p : window.GetComparisonProgress())
            if (p.samples != 1 || p.complete)
                throw std::runtime_error("Camera did not reset all methods");
        settings.comparisonMilliseconds = 1;
        window.SetRayTracing(settings);
        for (int i = 0; i < 12; ++i)
            window.DrawFrame();
        for (auto& p : window.GetComparisonProgress())
            if (!p.complete || p.milliseconds < 1)
                throw std::runtime_error("Timed budget did not freeze");
        window.SetRenderMode(RenderMode::RayTracing);
        window.DrawFrame();
        window.SetRenderMode(RenderMode::Raster);
        window.DrawFrame();
        window.WaitIdle();
        if (window.ValidationErrorCount())
            throw std::runtime_error("GPU validation errors");
        std::cout << "PASS comparison fixed spp, time budgets, freeze, reset and mode restore\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
