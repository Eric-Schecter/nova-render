#include <iostream>
#include <limits>
#include <render/Window.hpp>
#include <stdexcept>
#include <string>
int main(int argc, char** argv)
{
    try
    {
        if (argc != 4)
            throw std::runtime_error("Expected API, environment and model paths");
        render::WindowConfig config;
        config.width          = 640;
        config.height         = 480;
        config.title          = "Nova | Environment validation";
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
            throw std::runtime_error("Unknown test API");
        render::Window window(config);
        window.EnableParameterPanel();
        window.EnableOrbitControls();
        auto draw = [&] {
            int frames = 0;
            for (int attempt = 0; attempt < 200 && frames < 3 && window.PollEvents(); ++attempt)
                if (window.DrawFrame() == render::FrameResult::Rendered)
                    ++frames;
            if (frames != 3)
                throw std::runtime_error("Environment frame did not render");
        };
        window.LoadEnvironment(argv[2]);
        draw(); // A skybox does not require a model.
        auto environment           = window.GetEnvironment();
        environment.showBackground = false;
        window.SetEnvironment(environment);
        draw();
        window.LoadEnvironment(argv[2]);
        window.LoadEnvironment(argv[2]);
        environment.showBackground = true;
        environment.rotation       = 90;
        window.SetEnvironment(environment);
        draw();
        window.LoadModel(argv[3]);
        window.SetLighting(render::LightingMode::PBR);
        render::DirectionalLight light;
        light.intensity = 0;
        window.SetDirectionalLight(light);
        draw();
        window.LoadModel(argv[3]);
        window.LoadModel(argv[3]);
        draw();
        environment.intensity = 0;
        window.SetEnvironment(environment);
        draw();
        auto copy     = window.GetEnvironment();
        copy.rotation = 0;
        if (window.GetEnvironment().rotation != 90)
            throw std::runtime_error("Environment settings not copied");
        bool rejected = false;
        try
        {
            window.LoadEnvironment(std::string(argv[2]) + ".missing");
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Missing environment accepted");
        draw();
        environment.intensity = std::numeric_limits<float>::quiet_NaN();
        rejected              = false;
        try
        {
            window.SetEnvironment(environment);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected || window.GetEnvironment().intensity != 0)
            throw std::runtime_error("Invalid settings corrupted environment");
        window.SetLighting(render::LightingMode::Unlit);
        draw();
        window.WaitIdle();
        if (window.ValidationErrorCount())
            throw std::runtime_error("GPU validation error");
        std::cout << "PASS sky-only, model/environment reload, background/IBL independence, invalid-load rollback and "
                     "GPU cleanup\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
