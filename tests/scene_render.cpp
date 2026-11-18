#include "Backend.hpp"
#include "TestDevice.hpp"
#include "TestWindowHost.hpp"
#include <fstream>
#include <iostream>
#include <stb_image_write.h>
#include <stdexcept>

int main(int argc, char** argv)
{
    std::cout.setf(std::ios::unitbuf);
    render::detail::PresentationWindow window;
    try
    {
        if (argc < 3)
            throw std::invalid_argument("Expected API and assets directory");
        const std::string           api    = TestAPI(argv[1]);
        const std::filesystem::path assets = argv[2];
        render::RendererConfig      config;
        config.api     = api == "metal"    ? render::GraphicsAPI::Metal
                         : api == "vulkan" ? render::GraphicsAPI::Vulkan
                                           : render::GraphicsAPI::D3D12;
        config.useWarp = TestUseWarp(argv[1]);
        if (!InitTestWindowHost())
            throw std::runtime_error("Window host initialization failed");
        window = OpenTestWindow(api, 320, 240, "Scene API validation");
        if (!window)
            throw std::runtime_error("Window creation failed");
        std::unique_ptr<render::detail::Backend> backend;
#ifdef __APPLE__
        if (api == "metal")
            backend = render::detail::CreateMetalBackend(window, config);
#endif
#ifdef NOVA_TEST_VULKAN
        if (api == "vulkan")
            backend = render::detail::CreateVulkanBackend(window, config);
#endif
#ifdef _WIN32
        if (api == "dx12")
            backend = render::detail::CreateD3D12Backend(window, config);
#endif
        if (!backend)
            throw std::runtime_error("Backend unavailable");
        backend->ConfigurePathTracing(config.api, config);
        std::vector<render::SceneAsset> models{
          {"red", assets / "SceneSample/Red.glb"}, {"blue", assets / "SceneSample/Blue.glb"}};
        std::vector<render::SceneInstance> instances{{"upper", "red"}, {"lower", "red"}, {"right", "blue"}};
        for (size_t i = 0; i < instances.size(); ++i)
        {
            auto& world = instances[i].world;
            world[0] = world[5] = world[10] = .55f;
            world[12]                       = i == 2 ? 1.2f : -1.2f;
            world[13]                       = i == 0 ? .8f : i == 1 ? -.8f : 0;
        }
        backend->SetScene(models, instances);
        auto camera     = backend->GetCamera();
        camera.distance = 7;
        backend->SetCamera(camera);
        backend->SetClearColor({0, 0, 0, 1});
        auto draw = [&] {
            PollTestWindow();
            int w, h;
            TestPixelSize(window, &w, &h);
            backend->Resize(w, h);
            if (backend->DrawFrame() != render::FrameResult::Rendered)
                throw std::runtime_error("Skipped scene frame");
            backend->WaitIdle();
            if (backend->ValidationErrorCount())
                throw std::runtime_error("GPU validation errors");
        };
        draw(); // Off uses the direct raster pass.
        auto inspect = [&](bool expectEmpty = false) {
            const auto& image    = backend->GetRasterAAImage();
            size_t      redUpper = 0, redLower = 0, blue = 0;
            for (uint32_t y = 0; y < image.height; ++y)
                for (uint32_t x = 0; x < image.width; ++x)
                {
                    auto p = &image.pixels[(size_t(y) * image.width + x) * 4];
                    if (p[0] > 150 && p[0] > p[2] * 2 && x < image.width / 2)
                        (y < image.height / 2 ? redUpper : redLower)++;
                    if (p[2] > 150 && p[2] > p[0] * 2 && x > image.width / 2)
                        blue++;
                }
            if (expectEmpty ? redUpper + redLower + blue != 0 : redUpper < 50 || redLower < 50 || blue < 50)
            {
                std::filesystem::create_directories("validation/scene");
                stbi_write_png(
                  "validation/scene/failed.png", image.width, image.height, 4, image.pixels.data(), image.width * 4);
                throw std::runtime_error(
                  "Readback missed independent colored instances: upper=" + std::to_string(redUpper)
                  + "; lower=" + std::to_string(redLower) + "; blue=" + std::to_string(blue));
            }
            return image;
        };
        for (unsigned method = 1; method <= 5; ++method)
        {
            render::AntiAliasingSettings aa;
            aa.method = render::AAMethod(method);
            std::cout << "Scene AA method: " << method << std::endl;
            backend->SetAntiAliasing(aa);
            draw();
            inspect();
        }
        const auto normalized = inspect();
        std::filesystem::create_directories("validation/scene");
        stbi_write_png(
          ("validation/scene/" + api + "-instances.png").c_str(),
          normalized.width,
          normalized.height,
          4,
          normalized.pixels.data(),
          normalized.width * 4);
        for (auto& model : models)
            model.sourceSpace = true;
        for (auto& i : instances)
        {
            i.world[0] = i.world[5] = i.world[10] = .275f;
            i.world[12] -= .55f;
            i.world[13] -= .275f;
        }
        backend->SetScene(models, instances);
        draw();
        const auto source = inspect();
        if (source.pixels != normalized.pixels)
            throw std::runtime_error("Source-space equivalent transforms changed rendering");
        backend->ConfigurePathTracing(config.api, config);
        if (!backend->GetCapabilities().rayTracing)
            throw std::runtime_error("Scene ray tracing unavailable");
        render::SceneLight sun;
        sun.id        = "scene-sun";
        sun.direction = {0, 0, 1};
        sun.intensity = 4;
        backend->SetLights({sun});
        render::PathTracingSettings tracing;
        tracing.resolutionScale = .25f;
        tracing.maxBounces      = 1;
        backend->SetPathTracing(tracing);
        backend->SetRenderMode(render::RenderMode::RayTracing);
        draw();
        draw();
        if (backend->GetAccumulatedSamples() != 2)
            throw std::runtime_error("Scene ray accumulation failed");
        instances[0].world[12] += .2f;
        backend->SetScene(models, instances);
        if (backend->GetRenderMode() != render::RenderMode::RayTracing)
            throw std::runtime_error("Scene edit lost ray mode");
        draw();
        auto mat      = backend->GetMaterials("red")[0];
        mat.baseColor = {0, 1, 0, 1};
        backend->SetMaterial(0, mat, "red");
        draw();
        backend->SetRenderMode(render::RenderMode::Raster);
        backend->SetDirectionalLight({});
        backend->SetScene({}, {});
        draw();
        inspect(true);
        backend->LoadModel(render::detail::LoadGLB(assets / "SceneSample/Red.glb"));
        draw();
        if (backend->scene.active)
            throw std::runtime_error("LoadModel did not exit scene mode");
        backend->SetAntiAliasing({render::AAMethod::FXAA});
        render::Camera cameraView;
        cameraView.position           = {0, 0, 5};
        cameraView.projection         = render::Projection::Orthographic;
        cameraView.orthographicHeight = 4;
        backend->SetViewCamera(cameraView);
        backend->SetViewport(render::Viewport{30, 20, 120, 90});
        draw();
        if (backend->GetRasterAAImage().width != 120 || backend->GetRasterAAImage().height != 90)
            throw std::runtime_error("Viewport did not size AA render target");
        auto colored = [&] {
            size_t      n  = 0;
            const auto& im = backend->GetRasterAAImage();
            for (size_t k = 0; k < im.pixels.size(); k += 4)
                if (im.pixels[k] > 150 && im.pixels[k] > im.pixels[k + 2] * 2)
                    n++;
            return n;
        };
        const auto before = colored();
        if (before < 50)
            throw std::runtime_error("Orthographic camera invisible");
        cameraView.position[2] = 10;
        backend->SetViewCamera(cameraView);
        draw();
        if (colored() != before)
            throw std::runtime_error("Orthographic size changes with distance");
        cameraView.nearPlane = 12;
        cameraView.farPlane  = 20;
        backend->SetViewCamera(cameraView);
        draw();
        if (colored() != 0)
            throw std::runtime_error("Near clipping ignored");
        cameraView.nearPlane = .01f;
        cameraView.farPlane  = 8;
        backend->SetViewCamera(cameraView);
        draw();
        if (colored() != 0)
            throw std::runtime_error("Far clipping ignored");
        cameraView.farPlane    = 200;
        cameraView.orientation = {0, 0, .38268343f, .9238795f};
        backend->SetViewCamera(cameraView);
        draw();
        if (colored() < 50)
            throw std::runtime_error("Camera roll lost geometry");
        backend->SetViewport(render::Viewport{10, 10, 10000, 10000});
        draw();
        backend->SetViewport(std::nullopt);
        draw();
        backend->SetCamera({});
        draw();
        cameraView.position    = {0, 10, 0};
        cameraView.orientation = {-.70710678f, 0, 0, .70710678f};
        backend->SetViewCamera(cameraView);
        draw();
        if (colored() < 50)
            throw std::runtime_error("Vertical camera lost geometry");
        backend->SetAntiAliasing({render::AAMethod::TAA});
        backend->SetViewCamera(render::Camera{});
        draw();
        backend->SetViewCamera(backend->GetViewCamera());
        backend->SetViewport(backend->GetViewport());
        draw();
        if (backend->GetAntiAliasingHistoryFrames() != 2)
            throw std::runtime_error("Unchanged camera reset temporal history");
        backend->SetViewport(render::Viewport{100000, 100000, 1, 1});
        if (backend->DrawFrame() != render::FrameResult::Skipped)
            throw std::runtime_error("Outside viewport did not suspend frame");
        backend->SetViewport(std::nullopt);
        draw();
        if (backend->GetCapabilities().rayTracing)
        {
            cameraView.orientation = {0, 0, 0, 1};
            cameraView.position    = {0, 0, 5};
            cameraView.nearPlane   = .01f;
            cameraView.farPlane    = 200;
            backend->SetViewCamera(cameraView);
            backend->SetRenderMode(render::RenderMode::RayTracing);
            draw();
            if (!backend->GetAccumulatedSamples())
                throw std::runtime_error("Orthographic ray tracing did not accumulate");
            cameraView.nearPlane = 6;
            cameraView.farPlane  = 10;
            backend->SetViewCamera(cameraView);
            draw();
            backend->SetRenderMode(render::RenderMode::Raster);
        }
        backend.reset();
        CloseTestWindow(window);
        window = {};
        QuitTestWindowHost();
        std::cout << "PASS " << api
                  << " scene render: grouped draws, all AA methods, source equivalence, clear and legacy model\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        if (window)
            CloseTestWindow(window);
        QuitTestWindowHost();
        return 1;
    }
}
