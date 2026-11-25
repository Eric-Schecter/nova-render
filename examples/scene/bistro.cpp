#include <fstream>
#include <iostream>
#include <render/Window.hpp>
#include <set>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv)
{
    try
    {
        if (argc < 2 || argc > 4)
            throw std::invalid_argument("Usage: nova_bistro <prepared Bistro directory> [metal|vulkan|dx12] [environment.nenv]");
        render::WindowConfig config;
        config.width  = 960;
        config.height = 540;
        config.title  = "Nova Bistro instancing";
        if (argc >= 3)
        {
            const std::string_view api = argv[2];
            if (api == "metal")
                config.renderer.api = render::GraphicsAPI::Metal;
            else if (api == "vulkan")
                config.renderer.api = render::GraphicsAPI::Vulkan;
            else if (api == "dx12")
                config.renderer.api = render::GraphicsAPI::D3D12;
            else
                throw std::invalid_argument("Unknown graphics API");
        }

        const std::filesystem::path directory = argv[1];
        std::ifstream manifest(directory / "instances.txt");
        if (!manifest)
            throw std::runtime_error("Cannot open Bistro instances.txt");
        const bool lit = argc == 4;
        std::set<std::string> casters;
        if (lit)
        {
            std::ifstream input(directory / "casters.txt");
            if (!input)
                throw std::runtime_error("Missing Bistro casters.txt. Rerun prepare-bistro-scene.mjs.");
            for (std::string id; input >> id;)
                casters.insert(id);
        }
        std::set<std::string> ids;
        std::vector<render::SceneAsset> assets;
        std::vector<render::SceneInstance> instances;
        for (std::string asset, filename, instance; manifest >> asset >> filename >> instance;)
        {
            if (ids.insert(asset).second)
                assets.push_back({asset, directory / filename, true});
            render::SceneInstance row{instance, asset};
            for (auto& value : row.world)
            {
                double parsed;
                if (!(manifest >> parsed))
                    throw std::runtime_error("Incomplete Bistro transform for " + instance);
                value = static_cast<float>(parsed);
            }
            row.castShadow = casters.count(asset) != 0;
            instances.push_back(row);
        }
        if (instances.empty())
            throw std::runtime_error("No Bistro instances");

        render::Window window(config);
        render::Camera camera;
        // Authored Bistro overview camera in source coordinates.
        camera.position    = {-26.4307728f, 3.1561611f, 11.1703424f};
        camera.orientation = {.00482443f, -.80179983f, -.02351025f, .59711057f};
        camera.verticalFov = 35.9834f;
        camera.nearPlane   = .1f;
        camera.farPlane    = 1000;
        window.SetViewCamera(camera);
        window.SetLighting(lit ? render::LightingMode::PBR : render::LightingMode::Unlit);
        if (lit)
        {
            window.LoadEnvironment(argv[3]);
            window.SetEnvironment({0, .65f, 1, false});
            window.SetDirectionalLight({{-1, 1.8f, 1.3f}, {1, .95f, .85f}, 3});
            window.SetShadows({true, 1, 2, .015f});
        }
        window.SetScene(assets, instances);
        std::cout << "Bistro scene: " << assets.size() << " assets, " << instances.size() << " instances\n";
        while (window.PollEvents())
            window.DrawFrame();
        window.WaitIdle();
        return window.ValidationErrorCount() ? 1 : 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
