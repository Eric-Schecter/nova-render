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
        config.title    = "Installed Nova Render SDK consumer";
        render::Window window(config);
        window.PresentImage(image);
        int frames = 0;
        while (frames < 10 && window.PollEvents())
            if (window.DrawFrame() == render::FrameResult::Rendered)
                frames++;
        window.WaitIdle();
        if (argc > 2)
        {
            const std::filesystem::path           models = argv[2];
            const std::vector<render::SceneAsset> assets{{"box", models / "Red.glb", true}};
            std::vector<render::SceneInstance>    instances{{"left", "box"}, {"right", "box"}};
            instances[0].world[12] = -4;
            window.SetScene(assets, instances);
            window.ClearImage();
            const auto bounds = window.GetSceneAssetBounds("box");
            if (!bounds || bounds->maximum[0] != 4 || bounds->minimum[0] != 0)
                throw std::runtime_error("Installed scene API lost original model space");
            render::AntiAliasingSettings aa;
            aa.method = render::AAMethod::TAA;
            window.SetAntiAliasing(aa);
            for (int i = 0; i < 3; ++i)
            {
                window.PollEvents();
                window.DrawFrame();
            }
            instances[1].world[12] = 1;
            window.SetScene(assets, instances);
            window.DrawFrame();
            window.SetScene({}, {});
            window.DrawFrame();
            window.WaitIdle();
            if (window.GetSceneAssetBounds("box"))
                throw std::runtime_error("Installed scene clear failed");
        }
        render::Camera camera;
        camera.projection         = render::Projection::Orthographic;
        camera.orthographicHeight = 5;
        camera.verticalFov        = 60;
        camera.nearPlane          = .1f;
        camera.farPlane           = 100;
        camera.orientation        = {0, 0, .38268343f, .9238795f};
        window.SetViewCamera(camera);
        window.SetViewport(render::Viewport{20, 15, 160, 120});
        window.DrawFrame();
        window.WaitIdle();
        if (!window.GetViewport() || window.GetViewCamera().projection != render::Projection::Orthographic)
            throw std::runtime_error("Installed camera/viewport API failed");
        window.SetViewport(std::nullopt);
        window.DrawFrame();
        window.WaitIdle();
        if (frames != 10 || window.ValidationErrorCount())
            throw std::runtime_error("Installed window API failed");
        std::cout << "PASS installed SDK: two reflection probes, scene instances and original model space, window\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
