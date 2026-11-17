#include "Backend.hpp"
#include "TestDevice.hpp"
#include <GLFW/glfw3.h>
#include <fstream>
#include <iostream>
#include <stb_image_write.h>
using namespace render;
static double Difference(const Image& a, const Image& b)
{
    if (a.pixels.size() != b.pixels.size())
        throw std::runtime_error("Size mismatch");
    double d = 0;
    for (size_t i = 0; i < a.pixels.size(); i++)
        d += std::abs(int(a.pixels[i]) - int(b.pixels[i]));
    return d / a.pixels.size();
}
int main(int argc, char** argv)
{
    GLFWwindow* window = nullptr;
    std::cout.setf(std::ios::unitbuf);
    try
    {
        const std::string           api    = argc > 1 ? TestAPI(argv[1]) : "metal";
        const std::filesystem::path assets = argc > 2 ? argv[2] : "assets";
        RendererConfig              config;
        config.api = api == "metal" ? GraphicsAPI::Metal : api == "vulkan" ? GraphicsAPI::Vulkan : GraphicsAPI::D3D12;
        config.useWarp = argc > 1 && TestUseWarp(argv[1]);
        if (!glfwInit())
            throw std::runtime_error("GLFW init");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(160, 120, "Deferred validation", nullptr, nullptr);
        if (!window)
            throw std::runtime_error("Window init");
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
        if (!backend)
            throw std::runtime_error("Backend unavailable");
        backend->ConfigurePathTracing(config.api, config);
        PostProcessingSettings post;
        post.enabled      = true;
        post.bloom        = false;
        post.autoExposure = false;
        post.toneMapping  = ToneMapping::ACES;
        backend->SetPostProcessing(post);
        auto dir = std::filesystem::path("build/validation/deferred") / api;
        std::filesystem::create_directories(dir);
        auto draw = [&](const std::string& name) {
            glfwPollEvents();
            int w, h;
            glfwGetFramebufferSize(window, &w, &h);
            backend->Resize(w, h);
            if (backend->DrawFrame() != FrameResult::Rendered)
                throw std::runtime_error("Skipped frame");
            backend->WaitIdle();
            if (backend->ValidationErrorCount())
                throw std::runtime_error("GPU validation errors");
            auto image = backend->GetRasterAAImage();
            if (image.pixels.empty())
                throw std::runtime_error("Empty image");
            stbi_write_png(
              (dir / (name + ".png")).string().c_str(),
              image.width,
              image.height,
              4,
              image.pixels.data(),
              image.width * 4);
            return image;
        };
        std::ofstream metrics(dir / "metrics.json");
        metrics << "[";
        bool first = true;
        for (int fixture = 0; fixture < 5; fixture++)
        {
            if (fixture < 2)
            {
                backend->LoadModel(detail::LoadGLB(assets / "DamagedHelmet/DamagedHelmet.glb"));
                backend->SetLighting(fixture == 0 ? LightingMode::Unlit : LightingMode::PBR);
            }
            else if (fixture == 3)
            {
                backend->LoadModel(detail::LoadGLB(assets / "Fox/Fox.glb"));
                backend->SetLighting(LightingMode::PBR);
                auto state    = backend->animation.state;
                state.time    = .25f;
                state.playing = false;
                backend->SetAnimationState(state);
            }
            else
            {
                std::vector<SceneAsset> models{
                  {"red", assets / "SceneSample/Red.glb"}, {"blue", assets / "SceneSample/Blue.glb"}};
                std::vector<SceneInstance> instances{{"a", "red"}, {"b", "blue"}};
                instances[0].world[12] = -.7f;
                instances[1].world[12] = .7f;
                for (auto& i : instances)
                    i.world[0] = i.world[5] = i.world[10] = .5f;
                backend->SetScene(models, instances);
                backend->SetLighting(LightingMode::PBR);
            }
            if (fixture == 4)
            {
                SceneLight sun;
                sun.id             = "sun";
                sun.direction      = {.2f, 1, 1};
                sun.castShadow     = true;
                sun.shadowSoftness = 0;
                SceneLight point;
                point.id             = "point";
                point.type           = LightType::Point;
                point.position       = {-1, 1, 2};
                point.color          = {1, .3f, .2f};
                point.intensity      = 6;
                point.castShadow     = true;
                point.shadowSoftness = 0;
                SceneLight spot      = point;
                spot.id              = "spot";
                spot.type            = LightType::Spot;
                spot.position        = {1, 2, 2};
                spot.direction       = {0, -1, -1};
                spot.innerConeAngle  = .4f;
                spot.outerConeAngle  = .8f;
                spot.color           = {.2f, .3f, 1};
                backend->SetLights({sun, point, spot});
                SceneShadowSettings shadows;
                shadows.enabled = true;
                shadows.samples = 1;
                backend->SetShadows(shadows);
                FogSettings fog;
                fog.mode         = FogMode::Linear;
                fog.nearDistance = 1;
                fog.farDistance  = 10;
                backend->SetFog(fog);
                backend->LoadEnvironment(detail::LoadEnvironmentFile(assets / "Environment/studio_small_09.nenv"));
                auto material      = backend->GetMaterials("red")[0];
                material.baseColor = {.2f, .7f, .3f, 1};
                material.roughness = .4f;
                material.metallic  = .6f;
                backend->SetMaterial(0, material, "red");
            }
            for (auto method : {AAMethod::Off, AAMethod::FXAA, AAMethod::SMAA, AAMethod::SSAA, AAMethod::TAA})
            {
                backend->SetRasterSettings({});
                AntiAliasingSettings aa;
                aa.method = method;
                backend->SetAntiAliasing(aa);
                auto reference = draw("forward-" + std::to_string(fixture) + "-" + std::to_string(unsigned(method)));
                RasterSettings raster;
                raster.pipeline = RasterPipeline::Deferred;
                backend->SetRasterSettings(raster);
                auto   actual = draw("deferred-" + std::to_string(fixture) + "-" + std::to_string(unsigned(method)));
                double error  = Difference(reference, actual);
                std::cout << api << " fixture " << fixture << " AA " << unsigned(method) << " MAE " << error
                          << std::endl;
                if (error > 1)
                    throw std::runtime_error("Forward/deferred parity failed");
                if (!first)
                    metrics << ",";
                first = false;
                metrics << "{\"fixture\":" << fixture << ",\"aa\":" << unsigned(method) << ",\"mae\":" << error << "}";
            }
        }
        metrics << "]";
        AntiAliasingSettings aa;
        backend->SetAntiAliasing(aa);
        RasterSettings raster;
        raster.pipeline = RasterPipeline::Deferred;
        for (unsigned view = 1; view <= 9; view++)
        {
            raster.view = GBufferView(view);
            backend->SetRasterSettings(raster);
            draw("debug-" + std::to_string(view));
        }
        raster.view = GBufferView::Final;
        backend->SetRasterSettings(raster);
        auto camera               = backend->GetViewCamera();
        camera.projection         = Projection::Orthographic;
        camera.orthographicHeight = 3;
        camera.nearPlane          = .1f;
        camera.farPlane           = 60;
        backend->SetViewCamera(camera);
        backend->SetViewport(Viewport{24, 18, 240, 180});
        backend->SetRasterSettings({});
        auto reference = draw("viewport-forward");
        backend->SetRasterSettings(raster);
        if (Difference(reference, draw("viewport-deferred")) > 1)
            throw std::runtime_error("Viewport/orthographic parity failed");
        backend->SetViewport(std::nullopt);
        backend->SetScene({}, {});
        auto environment           = backend->GetEnvironment();
        environment.showBackground = false;
        backend->SetEnvironment(environment);
        auto empty = draw("empty-scene");
        for (size_t i = 0; i < empty.pixels.size(); i++)
            if (i % 4 != 3 && empty.pixels[i] > 40)
                throw std::runtime_error("Empty scene retained G-buffer geometry");
        bool rejected = false;
        aa.method     = AAMethod::MSAA;
        try
        {
            backend->SetAntiAliasing(aa);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Deferred MSAA must be rejected");
        backend.reset();
        glfwDestroyWindow(window);
        window = nullptr;
        glfwTerminate();
        std::cout << "Deferred model checks passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        if (window)
            glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
}
