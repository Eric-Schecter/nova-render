#include "Backend.hpp"
#include "TestDevice.hpp"
#include "TestWindowHost.hpp"
#include <fstream>
#include <iostream>
#include <stb_image_write.h>
#include <stdexcept>
using namespace render;
void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}
int main(int argc, char** argv)
{
    std::cout.setf(std::ios::unitbuf);
    render::detail::PresentationWindow window;
    try
    {
        Require(argc == 3, "Expected API and asset directory");
        std::string           api    = TestAPI(argv[1]);
        std::filesystem::path path   = std::filesystem::path(argv[2]) / "MaterialSample/Dual.glb";
        auto                  source = detail::LoadGLB(path);
        Require(source->materials.size() == 2, "Missing slots");
        Require(
          source->vertices[0].position[3] == 1 && source->vertices[36].position[3] == 2, "Incorrect slot assignment");
        Require(InitTestWindowHost(), "Window host initialization");
        window = OpenTestWindow(api, 480, 360, "Multi-light validation");
        Require(bool(window), "Window failed");
        RendererConfig config;
        config.api     = api == "metal" ? GraphicsAPI::Metal : api == "dx12" ? GraphicsAPI::D3D12 : GraphicsAPI::Vulkan;
        config.useWarp = TestUseWarp(argv[1]);
        std::unique_ptr<detail::Backend> backend;
#ifdef __APPLE__
        if (api == "metal")
            backend = detail::CreateMetalBackend(window, config);
#endif
#ifdef NOVA_TEST_VULKAN
        if (api == "vulkan")
            backend = detail::CreateVulkanBackend(window, config);
#endif
#ifdef _WIN32
        if (api == "dx12")
            backend = detail::CreateD3D12Backend(window, config);
#endif
        Require(bool(backend), "Backend unavailable");
        backend->ConfigurePathTracing(config.api, config);
        backend->LoadModel(source);
        Camera camera;
        camera.position           = {0, 0, 4};
        camera.projection         = Projection::Orthographic;
        camera.orthographicHeight = 2.6f;
        backend->SetViewCamera(camera);
        auto draw = [&]() {
            PollTestWindow();
            int w, h;
            TestPixelSize(window, &w, &h);
            backend->Resize(w, h);
            Require(backend->DrawFrame() == FrameResult::Rendered, "Skipped frame");
            backend->WaitIdle();
            Require(!backend->ValidationErrorCount(), "GPU errors");
        };
        backend->SetLighting(LightingMode::PBR);
        backend->SetAntiAliasing({AAMethod::FXAA});
        auto material = backend->GetMaterials("");
        for (size_t i = 0; i < material.size(); ++i)
        {
            material[i].baseColor = {.6f, .6f, .6f, 1};
            material[i].metallic  = 0;
            material[i].roughness = .8f;
            backend->SetMaterial(i, material[i], "");
        }
        Require(backend->GetLightingCapabilities().maxLights == 16, "Missing raster light capabilities");
        std::vector<SceneLight> lights(6);
        lights[0].id             = "d1";
        lights[0].direction      = {-1, 1, 2};
        lights[0].color          = {1, 0, 0};
        lights[0].intensity      = 2;
        lights[1].id             = "d2";
        lights[1].direction      = {1, 1, 2};
        lights[1].color          = {0, 1, 0};
        lights[1].intensity      = 2;
        lights[2].id             = "p1";
        lights[2].type           = LightType::Point;
        lights[2].position       = {-1, 0, 1};
        lights[2].color          = {0, 0, 1};
        lights[2].intensity      = 4;
        lights[3].id             = "p2";
        lights[3].type           = LightType::Point;
        lights[3].position       = {1, 0, 1};
        lights[3].color          = {1, 0, 0};
        lights[3].intensity      = 4;
        lights[4].id             = "s1";
        lights[4].type           = LightType::Spot;
        lights[4].position       = {-1, 0, 2};
        lights[4].direction      = {.3f, 0, -1};
        lights[4].color          = {0, 1, 0};
        lights[4].intensity      = 8;
        lights[4].outerConeAngle = .7f;
        lights[5]                = lights[4];
        lights[5].id             = "s2";
        lights[5].position       = {1, 0, 2};
        lights[5].direction      = {-.3f, 0, -1};
        lights[5].color          = {0, 0, 1};
        auto image               = [&](const std::string& name) {
            draw();
            auto im = backend->GetRasterAAImage();
            std::filesystem::create_directories("validation/lights");
            stbi_write_png(
              ("validation/lights/" + api + (config.useWarp ? "-warp-" : "-") + name + ".png").c_str(),
              im.width,
              im.height,
              4,
              im.pixels.data(),
              im.width * 4);
            return im;
        };
        auto difference = [](const Image& a, const Image& b) {
            double sum = 0;
            for (size_t i = 0; i < a.pixels.size(); ++i)
                sum += std::abs(int(a.pixels[i]) - int(b.pixels[i]));
            return sum / a.pixels.size();
        };
        backend->SetLights({});
        const auto         empty = image("empty");
        std::vector<Image> singles;
        for (size_t i = 0; i < lights.size(); ++i)
        {
            backend->SetLights({lights[i]});
            singles.push_back(image("single-" + std::to_string(i)));
            Require(difference(empty, singles.back()) > .15, "Single light did not contribute");
        }
        backend->SetLights(lights);
        const auto all = image("all");
        for (size_t i = 0; i < lights.size(); ++i)
        {
            auto subset = lights;
            subset.erase(subset.begin() + i);
            backend->SetLights(subset);
            Require(difference(all, image("without-" + std::to_string(i))) > .05, "Light missing from sum");
        }
        backend->SetLights(lights);
        auto bad      = lights;
        bad[0].id     = bad[1].id;
        bool rejected = false;
        try
        {
            backend->SetLights(bad);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        Require(rejected && backend->GetLights().size() == 6, "Nontransactional validation");
        auto shadowPoint       = lights[2];
        shadowPoint.castShadow = true;
        backend->SetLights({shadowPoint});
        Require(
          backend->GetLights()[0].castShadow && backend->GetLightingCapabilities().pointShadows
            && backend->GetLightingCapabilities().spotShadows,
          "Local shadow capability mismatch");
        backend->SetRenderMode(RenderMode::RayTracing);
        Require(
          backend->GetLightingCapabilities().point && !backend->GetLightingCapabilities().shadowRequiresScene,
          "Ray lighting capability mismatch");
        backend->SetLights(lights);
        draw();
        backend->SetRenderMode(RenderMode::Raster);
        auto disabled = lights;
        for (auto& light : disabled)
            light.enabled = false;
        backend->SetLights(disabled);
        Require(difference(empty, image("disabled")) < .01, "Disabled collection added hidden light");
        auto point  = lights[2];
        point.range = .1f;
        backend->SetLights({point});
        Require(difference(singles[2], image("range")) > .1, "Range ignored");
        auto spot      = lights[4];
        spot.direction = {0, 0, 1};
        backend->SetLights({spot});
        Require(difference(singles[4], image("away")) > .1, "Spot direction ignored");
        backend->SetDirectionalLight({});
        Require(backend->GetLights().size() == 1, "Legacy replacement failed");
        image("legacy");
        backend.reset();
        CloseTestWindow(window);
        window = {};
        QuitTestWindowHost();
        std::cout << "PASS " << api << " six-light accumulation, types, range, cone, validation and capabilities\n";
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
