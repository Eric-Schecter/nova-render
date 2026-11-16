#include "../examples/comparison/ComparisonScene.hpp"
#include "TestDevice.hpp"
#include <iostream>
#include <render/Window.hpp>
int main(int argc, char** argv)
{
    try
    {
        if (argc < 3)
            return 1;
        render::WindowConfig config;
        config.width            = 96;
        config.height           = 96;
        const std::string api   = TestAPI(argv[1]);
        config.renderer.api     = api == "metal"    ? render::GraphicsAPI::Metal
                                  : api == "vulkan" ? render::GraphicsAPI::Vulkan
                                                    : render::GraphicsAPI::D3D12;
        config.renderer.useWarp = TestUseWarp(argv[1]);
        example::ComparisonScene scene(config.renderer);
        if (!scene.Supported())
        {
            std::cout << "SKIP: four-integrator comparison requires MLT support (DX12 WARP excluded)" << std::endl;
            return 77;
        }
        const auto path = std::filesystem::path(argv[2]) / "CornellCaustics/CornellCaustics.glb";
        scene.LoadModel(path);
        render::RayTracingSettings settings;
        settings.maxBounces          = 8;
        settings.resolutionScale     = 1;
        settings.photonsPerIteration = 256;
        settings.mltBootstrapSamples = 512;
        scene.SetSettings(settings);
        scene.SetBudget({3, 0});
        render::OrbitCamera camera;
        camera.exposure = 3;
        scene.SetCamera(camera);
        for (int i = 0; i < 5; ++i)
            scene.DrawFrame(32);
        const auto image    = scene.DrawFrame(32);
        const auto progress = scene.Progress();
        for (const auto& p : progress)
            if (p.samples != 3 || !p.complete)
                throw std::runtime_error("Budget did not freeze");
        if (image.pixels != scene.DrawFrame(32).pixels)
            throw std::runtime_error("Frozen image changed");
        for (int i = 0; i < 4; ++i)
        {
            double sum = 0;
            for (uint32_t y = 0; y < 32; ++y)
                for (uint32_t x = 0; x < 32; ++x)
                    sum += image.pixels[((y + (i / 2) * 32) * 64 + x + (i % 2) * 32) * 4];
            if (sum == 0)
                throw std::runtime_error("Empty tile");
        }
        camera.yaw = 5;
        scene.SetCamera(camera);
        scene.DrawFrame(32);
        for (const auto& p : scene.Progress())
            if (p.samples != 1 || p.complete)
                throw std::runtime_error("Camera did not reset views");
        scene.SetBudget({64, 1});
        for (int i = 0; i < 3; ++i)
            scene.DrawFrame(32);
        for (const auto& p : scene.Progress())
            if (!p.complete || p.milliseconds < 1)
                throw std::runtime_error("Timed budget failed");
        scene.DrawFrame(24);
        for (const auto& p : scene.Progress())
            if (p.samples < 1)
                throw std::runtime_error("Resize failed");
        render::Window window(config);
        window.LoadModel(path);
        window.EnableParameterPanel();
        window.PresentImage(image);
        window.PollEvents();
        window.DrawFrame();
        window.ClearImage();
        window.SetRenderMode(render::RenderMode::RayTracing);
        window.SetRayTracing(settings);
        window.DrawFrame();
        window.SetRenderMode(render::RenderMode::Raster);
        window.DrawFrame();
        bool rejected = false;
        try
        {
            window.SetRenderMode(static_cast<render::RenderMode>(2));
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Unexpected third render mode");
        rejected = false;
        try
        {
            window.PresentImage({3, 3, {1, 2, 3}});
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Invalid image accepted");
        window.WaitIdle();
        if (window.ValidationErrorCount())
            throw std::runtime_error("GPU validation errors");
        std::cout << "PASS example-owned comparison: 4 views, 2x2 pixels, samples/time/freeze/reset/resize, generic "
                     "presentation and two-mode API\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
