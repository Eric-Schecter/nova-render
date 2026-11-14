#include "path/PathTracing.hpp"
#include <fstream>
#include <iostream>
#include <render/Window.hpp>
#include <stdexcept>
using namespace render;
int main(int argc, char** argv)
{
    try
    {
        if (argc != 3 && argc != 4)
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
        for (int i = 0; i < 4; ++i)
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
        if (argc == 4)
        {
            render::detail::PathSession capture;
            capture.Capabilities(config.renderer.api, config.renderer);
            auto model =
              render::detail::LoadGLB(std::filesystem::path(argv[2]) / "CornellCaustics/CornellCaustics.glb");
            OrbitCamera c;
            c.exposure                      = 3;
            auto uniforms                   = render::detail::MakeSceneUniforms(*model, c, 3, LightingMode::PBR);
            settings.comparisonMilliseconds = 0;
            settings.comparisonSamples      = 64;
            settings.photonsPerIteration    = 8192;
            settings.mltBootstrapSamples    = 262144;
            settings.photonRadius           = .1f;
            settings.resolutionScale        = .5f;
            const render::detail::ModelImage* frame = nullptr;
            for (int i = 0; i < 64; ++i)
                frame = &capture.RenderComparison(model, uniforms, settings, 1440, 480);
            std::ofstream out(argv[3], std::ios::binary);
            out.write(reinterpret_cast<const char*>(&frame->width), 4);
            out.write(reinterpret_cast<const char*>(&frame->height), 4);
            out.write(reinterpret_cast<const char*>(frame->pixels.data()), frame->pixels.size());
            const auto paused = frame->pixels;
            if (capture.RenderComparison(model, uniforms, settings, 1440, 480).pixels != paused)
                throw std::runtime_error("Frozen comparison pixels changed");
            for (int column = 0; column < 4; ++column)
            {
                double sum = 0;
                for (uint32_t y = 0; y < frame->height; ++y)
                    for (uint32_t x = column * frame->width / 4; x < (column + 1) * frame->width / 4; ++x)
                        sum += frame->pixels[(size_t(y) * frame->width + x) * 4];
                if (sum < 100)
                    throw std::runtime_error("Empty native comparison column");
            }
        }
        if (argc == 4)
        {
            render::detail::PathSession portrait;
            portrait.Capabilities(config.renderer.api, config.renderer);
            auto model =
              render::detail::LoadGLB(std::filesystem::path(argv[2]) / "CornellCaustics/CornellCaustics.glb");
            auto uniforms = render::detail::MakeSceneUniforms(*model, OrbitCamera{}, 1.f / 3, LightingMode::PBR);
            settings.comparisonMilliseconds = 0;
            settings.comparisonSamples      = 1;
            settings.resolutionScale        = .5f;
            settings.photonsPerIteration    = 256;
            const auto& frame               = portrait.RenderComparison(model, uniforms, settings, 96, 288);
            for (int row = 0; row < 4; ++row)
            {
                size_t sum = 0;
                for (uint32_t y = row * frame.height / 4; y < (row + 1) * frame.height / 4; ++y)
                    for (uint32_t x = 0; x < frame.width; ++x)
                        sum += frame.pixels[(size_t(y) * frame.width + x) * 4];
                if (sum < 50)
                    throw std::runtime_error("Empty portrait comparison view");
            }
        }
        std::cout << "PASS comparison fixed spp, time budgets, freeze, reset and mode restore\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
