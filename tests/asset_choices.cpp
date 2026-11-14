#include <iostream>
#include <render/Window.hpp>
#include <stdexcept>
int main(int argc, char** argv)
{
    try
    {
        if (argc != 3)
            throw std::runtime_error("Expected API and assets directory");
        render::WindowConfig config;
        config.width          = 800;
        config.height         = 600;
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
        render::Window               window(config);
        const std::filesystem::path  root = argv[2];
        render::ParameterPanelConfig panel;
        for (const auto* name : {"DamagedHelmet", "Avocado", "BoomBox", "CornellBox"})
            panel.models.push_back({name, root / name / (std::string(name) + ".glb")});
        for (const auto* name : {"venice_sunset", "kloppenheim_06", "studio_small_09"})
            panel.environments.push_back({name, root / "Environment" / (std::string(name) + ".nenv")});
        window.LoadModel(panel.models[0].path);
        window.LoadEnvironment(panel.environments[0].path);
        window.SetLighting(render::LightingMode::PBR);
        render::OrbitCamera camera;
        camera.yaw      = 37;
        camera.exposure = 1.2f;
        window.SetCamera(camera);
        render::DirectionalLight light;
        light.intensity = 0;
        window.SetDirectionalLight(light);
        render::EnvironmentSettings environment;
        environment.rotation  = 45;
        environment.intensity = 0.7f;
        window.SetEnvironment(environment);
        window.EnableParameterPanel(panel);
        auto draw = [&] {
            int frames = 0;
            for (int attempt = 0; attempt < 100 && frames < 2 && window.PollEvents(); ++attempt)
                if (window.DrawFrame() == render::FrameResult::Rendered)
                    ++frames;
            if (frames != 2)
                throw std::runtime_error("Choice did not render");
        };
        // All nine combinations exercise the same resource replacement used by the UI.
        for (const auto& model : panel.models)
        {
            window.LoadModel(model.path);
            for (const auto& map : panel.environments)
            {
                window.LoadEnvironment(map.path);
                draw();
                if (
                  window.GetCamera().yaw != camera.yaw || window.GetCamera().exposure != camera.exposure
                  || window.GetDirectionalLight().intensity != 0 || window.GetEnvironment().rotation != 45)
                    throw std::runtime_error("Asset change reset scene parameters");
            }
        }
        window.LoadModel(panel.models.front().path);
        window.LoadEnvironment(panel.environments.front().path);
        draw();
        auto invalid = panel;
        invalid.models[0].name.clear();
        bool rejected = false;
        try
        {
            window.EnableParameterPanel(invalid);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Invalid catalog accepted");
        draw();
        window.EnableParameterPanel(false);
        draw();
        window.EnableParameterPanel(panel);
        draw();
        window.WaitIdle();
        if (window.ValidationErrorCount())
            throw std::runtime_error("GPU validation errors");
        std::cout << "PASS twelve model/environment combinations, preserved parameters and panel lifecycle\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
