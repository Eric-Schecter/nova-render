#include <fstream>
#include <iostream>
#include <render/RayTracingView.hpp>
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
            window.LoadModel(models / "Dual.glb");
            auto materialSlots = window.GetMaterials();
            if (materialSlots.size() != 2)
                throw std::runtime_error("Installed SDK has no material slots");
            auto material             = materialSlots.front();
            material.baseColor        = {.2f, .8f, .3f, 1};
            material.roughness        = .25f;
            material.metallic         = .7f;
            material.baseColorTexture = render::Image{1, 1, {255, 255, 255, 255}};
            window.SetMaterial(0, material);
            if (window.GetMaterials().front().roughness != .25f)
                throw std::runtime_error("Installed SDK material API failed");
            window.SetFog({});
            render::SceneShadowSettings shadows;
            shadows.enabled  = true;
            shadows.softness = 0;
            window.SetShadows(shadows);
            window.LoadEnvironment({});
            if (!window.GetShadows().enabled || window.GetFog().mode != render::FogMode::Off)
                throw std::runtime_error("Installed atmosphere API failed");
            window.SetScene(assets, instances);
            if (window.GetLightingCapabilities().maxLights > 0)
            {
                const auto caps = window.GetLightingCapabilities();
                if (!caps.point || !caps.spot || caps.maxLights != 16 || !caps.pointShadows || !caps.spotShadows)
                    throw std::runtime_error("Installed light capabilities mismatch");
                render::SceneLight point;
                point.id         = "installed-point";
                point.type       = render::LightType::Point;
                point.position   = {1, 2, 3};
                point.intensity  = 8;
                point.range      = 10;
                point.castShadow = true;
                auto spot        = point;
                spot.id          = "installed-spot";
                spot.type        = render::LightType::Spot;
                spot.direction   = {0, -1, -1};
                window.SetLights({point, spot});
                if (window.GetLights().size() != 2)
                    throw std::runtime_error("Installed scene lights missing");
            }
            render::Ray ray;
            ray.origin        = {2, 1, 5};
            ray.direction     = {0, 0, -2};
            const auto picked = window.PickScene(ray);
            if (
              !picked || picked->id != "right" || picked->distance != 4 || picked->position[2] != 1
              || picked->normal[2] != 1)
                throw std::runtime_error("Installed picking API failed");
            ray.excludeIds = {"right"};
            if (window.PickScene(ray))
                throw std::runtime_error("Installed picking exclusion failed");
            ray.excludeIds.clear();
            ray.includeIds = {"right"};
            if (!window.PickScene(ray))
                throw std::runtime_error("Installed picking inclusion failed");

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
            if (window.GetCapabilities().rayTracing)
            {
                render::RayTracingSettings tracing;
                tracing.maxBounces      = 1;
                tracing.resolutionScale = .25f;
                window.SetRayTracing(tracing);
                window.SetRenderMode(render::RenderMode::RayTracing);
                window.DrawFrame();
                window.DrawFrame();
                if (
                  window.GetAccumulatedSamples() != 2
                  || window.GetLightingCapabilities().estimator != "camera-path-nee")
                    throw std::runtime_error("Installed scene ray lights failed");
                window.SetRenderMode(render::RenderMode::Raster);
            }
            render::RayTracingView rayView(renderer);
            const auto             capabilities = rayView.GetCapabilities();
            if (capabilities.pathTracing)
            {
                rayView.LoadModel(models / "Dual.glb");
                render::RayTracingSettings settings;
                settings.maxBounces          = 2;
                settings.resolutionScale     = 1;
                settings.mltBootstrapSamples = 512;
                settings.integrator          = capabilities.softwareRayTracing ? render::RayIntegrator::PathTracing
                                                                               : render::RayIntegrator::Metropolis;
                rayView.SetRayTracing(settings);
                rayView.DrawFrame(96, 96);
                const auto&   rays   = rayView.DrawFrame(96, 96);
                size_t        energy = 0;
                std::ofstream capture("installed-rays.ppm", std::ios::binary);
                capture << "P6\n96 96\n255\n";
                for (size_t i = 0; i < rays.pixels.size(); i += 4)
                {
                    energy += rays.pixels[i] + rays.pixels[i + 1] + rays.pixels[i + 2];
                    capture.write(reinterpret_cast<const char*>(rays.pixels.data() + i), 3);
                }
                if (!energy || rayView.GetAccumulatedSamples() != 2)
                    throw std::runtime_error("Installed ray API failed");
                std::cout << "PASS installed ray API: " << capabilities.rayTracingDevice
                          << "; software=" << capabilities.softwareRayTracing << std::endl;
            }
            window.SetScene({}, {});
            if (window.PickScene(ray))
                throw std::runtime_error("Installed picking clear failed");
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
