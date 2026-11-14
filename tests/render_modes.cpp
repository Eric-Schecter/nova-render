#include <cmath>
#include <iostream>
#include <limits>
#include <render/Window.hpp>
#include <stdexcept>
int main(int argc, char** argv)
{
    try
    {
        if (argc != 3)
            throw std::runtime_error("Expected API and assets root");
        render::WindowConfig config;
        config.width          = 640;
        config.height         = 480;
        const std::string api = argv[1];
        if (api == "metal")
            config.renderer.api = render::GraphicsAPI::Metal;
        else if (api == "vulkan")
            config.renderer.api = render::GraphicsAPI::Vulkan;
        else if (api == "dx12")
        {
            config.renderer.api     = render::GraphicsAPI::D3D12;
            config.renderer.useWarp = true;
        }
        else
            throw std::runtime_error("Unknown API");
        render::Window              window(config);
        const std::filesystem::path root = argv[2];
        window.LoadModel(root / "DamagedHelmet/DamagedHelmet.glb");
        window.LoadEnvironment(root / "Environment/venice_sunset.nenv");
        window.SetLighting(render::LightingMode::PBR);
        const auto caps = window.GetCapabilities();
        std::cout << "Path support=" << caps.pathTracing << "; software=" << caps.softwareRayTracing
                  << "; device=" << caps.rayTracingDevice << std::endl;
        auto draw = [&] {
            int frames = 0;
            for (int attempts = 0; attempts < 100 && frames < 1 && window.PollEvents(); ++attempts)
                if (window.DrawFrame() == render::FrameResult::Rendered)
                    ++frames;
            if (frames != 1)
                throw std::runtime_error("Frame did not render");
        };
        if (window.GetRenderMode() != render::RenderMode::Raster)
            throw std::runtime_error("Default mode changed");
        if (!caps.pathTracing)
        {
            bool rejected = false;
            try
            {
                window.SetRenderMode(render::RenderMode::PathTracing);
            }
            catch (const std::runtime_error&)
            {
                rejected = true;
            }
            if (!rejected || window.GetRenderMode() != render::RenderMode::Raster)
                throw std::runtime_error("Unsupported mode corrupted state");
            draw();
            window.WaitIdle();
            std::cout << "PASS unavailable path mode rejected and raster preserved: " << caps.reason << '\n';
            return 0;
        }
        render::PathTracingSettings path;
        path.resolutionScale = .25f;
        path.maxBounces      = 2;
        window.SetPathTracing(path);
        window.SetRenderMode(render::RenderMode::PathTracing);
        window.EnableParameterPanel();
        draw();
        draw();
        if (window.GetAccumulatedSamples() != 2)
            throw std::runtime_error("Window path samples did not advance");
        auto camera = window.GetCamera();
        camera.yaw  = 30;
        window.SetCamera(camera);
        draw();
        if (window.GetAccumulatedSamples() != 1)
            throw std::runtime_error("Camera reset failed");
        auto env     = window.GetEnvironment();
        env.rotation = 50;
        window.SetEnvironment(env);
        draw();
        if (window.GetAccumulatedSamples() != 1)
            throw std::runtime_error("Environment reset failed");
        auto light      = window.GetDirectionalLight();
        light.intensity = 7;
        window.SetDirectionalLight(light);
        draw();
        if (window.GetAccumulatedSamples() != 1)
            throw std::runtime_error("Light reset failed");
        path.samplesPerFrame = 2;
        window.SetPathTracing(path);
        draw();
        if (window.GetAccumulatedSamples() != 2)
            throw std::runtime_error("Samples per frame mismatch");
        path.denoise = true;
        window.SetPathTracing(path);
        if (!window.GetPathTracing().denoise || window.GetAccumulatedSamples() != 0)
            throw std::runtime_error("Denoise API/reset mismatch");
        draw();
        path.denoise = false;
        window.SetPathTracing(path);
        draw();
        window.ResetAccumulation();
        if (window.GetAccumulatedSamples() != 0)
            throw std::runtime_error("Manual reset failed");
        draw();
        window.SetRenderMode(render::RenderMode::Raster);
        draw();
        if (window.GetAccumulatedSamples() != 0)
            throw std::runtime_error("Switch did not reset samples");
        window.SetRenderMode(render::RenderMode::PathTracing);
        draw();
        if (window.GetAccumulatedSamples() != 2)
            throw std::runtime_error("Path re-entry failed");
        for (const auto* name : {"DamagedHelmet", "Avocado", "BoomBox", "CornellBox"})
        {
            window.LoadModel(root / name / (std::string(name) + ".glb"));
            for (const auto* map : {"venice_sunset", "kloppenheim_06", "studio_small_09"})
            {
                window.LoadEnvironment(root / "Environment" / (std::string(map) + ".nenv"));
                draw();
                if (window.GetAccumulatedSamples() != 2)
                    throw std::runtime_error("Model/environment change did not reset accumulation");
            }
        }
        auto invalid            = path;
        invalid.resolutionScale = std::numeric_limits<float>::quiet_NaN();
        bool rejected           = false;
        try
        {
            window.SetPathTracing(invalid);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected || window.GetPathTracing().resolutionScale != path.resolutionScale)
            throw std::runtime_error("Invalid settings corrupted state");
        window.SetRenderMode(render::RenderMode::Raster);
        window.EnableParameterPanel(false);
        draw();
        window.WaitIdle();
        if (window.ValidationErrorCount())
            throw std::runtime_error("GPU validation error");
        std::cout << "PASS native mode presentation, bidirectional switch, parameter/model resets, invalid settings "
                     "and UI lifecycle\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
