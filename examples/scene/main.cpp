#include <iostream>
#include <render/Window.hpp>
#include <stdexcept>

int main(int argc, char** argv)
{
    try
    {
        if (argc < 2)
            throw std::invalid_argument("Usage: nova_scene <assets/SceneSample> [metal|vulkan|dx12]");
        render::WindowConfig config;
        config.title = "Nova Render - scene instances";
        if (argc > 2)
        {
            const std::string api = argv[2];
            if (api == "metal")
                config.renderer.api = render::GraphicsAPI::Metal;
            else if (api == "vulkan")
                config.renderer.api = render::GraphicsAPI::Vulkan;
            else if (api == "dx12")
                config.renderer.api = render::GraphicsAPI::D3D12;
            else
                throw std::invalid_argument("Unknown graphics API");
        }
        render::Window              window(config);
        const std::filesystem::path directory = argv[1];
        // Assets are registered once by ID. Instances share the same GPU geometry and textures.
        const std::vector<render::SceneAsset> assets{
          {"red", directory / "Red.glb", true}, {"blue", directory / "Blue.glb", true}};
        std::vector<render::SceneInstance> instances{
          {"red-upper", "red"}, {"red-lower", "red"}, {"blue-right", "blue"}};
        for (size_t i = 0; i < instances.size(); ++i)
        {
            auto& world = instances[i].world;
            world[0] = world[5] = world[10] = .275f;
            world[12]                       = (i == 2 ? 1.2f : -1.2f) - .55f;
            world[13]                       = (i == 0 ? .8f : i == 1 ? -.8f : 0) - .275f;
        }
        window.SetScene(assets, instances);
        const auto bounds = window.GetSceneAssetBounds("red");
        if (!bounds || bounds->maximum[0] != 4)
            throw std::runtime_error("Unexpected source-space bounds");
        auto camera     = window.GetCamera();
        camera.distance = 7;
        window.SetCamera(camera);
        window.EnableOrbitControls();
        window.EnableParameterPanel();
        while (window.PollEvents())
            window.DrawFrame();
        window.WaitIdle();
        return window.ValidationErrorCount() ? 1 : 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
