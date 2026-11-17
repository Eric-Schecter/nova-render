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
        window = glfwCreateWindow(160, 120, "GPU presentation validation", nullptr, nullptr);
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
        auto dir = std::filesystem::path("build/validation/hybrid-shadow") / api;
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


        backend->LoadModel(detail::LoadGLB(assets / "DamagedHelmet/DamagedHelmet.glb"));
        backend->SetLighting(LightingMode::PBR);
        backend->SetRenderMode(RenderMode::Hybrid);
        auto caps = backend->GetHybridCapabilities();
        if (
          !caps.supported || !caps.shadows || !caps.softwareRayTracing || caps.hardwareRayTracing
          || !caps.ambientOcclusion || !caps.reflections || caps.globalIllumination)
            throw std::runtime_error("Incorrect Hybrid capabilities");
        auto capture = [&](const std::string& name) {
            glfwPollEvents();
            int w, h;
            glfwGetFramebufferSize(window, &w, &h);
            backend->Resize(w, h);
            auto before = backend->RasterReadbackCount();
            if (backend->DrawFrame() != FrameResult::Rendered)
                throw std::runtime_error("Skipped Hybrid frame");
            backend->WaitIdle();
            if (backend->RasterReadbackCount() != before)
                throw std::runtime_error("Hybrid automatic readback");
            auto im = backend->CaptureRasterFrame();
            stbi_write_png(
              (dir / (name + ".png")).string().c_str(), im.width, im.height, 4, im.pixels.data(), im.width * 4);
            if (backend->ValidationErrorCount())
                throw std::runtime_error("GPU validation errors");
            return im;
        };
        HybridSettings settings;
        settings.shadows = false;
        backend->SetHybrid(settings);
        auto off         = capture("single-off");
        settings.shadows = true;
        backend->SetHybrid(settings);
        auto on     = capture("single-shadow");
        auto matrix = [](float x, float y, float z, float tx, float ty, float tz) {
            return std::array<float, 16>{x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, tx, ty, tz, 1};
        };
        backend->SetScene(
          {{"box", assets / "SceneSample/Red.glb"}},
          {{"ground", "box", matrix(2, .06f, 2, 0, -.8f, 0)}, {"caster", "box", matrix(.3f, 1, .3f, 0, 0, 0)}});
        auto material      = backend->GetMaterials("box")[0];
        material.baseColor = {.8f, .8f, .8f, 1};
        material.metallic  = 0;
        material.roughness = .7f;
        backend->SetMaterial(0, material, "box");
        OrbitCamera camera;
        camera.yaw      = 30;
        camera.pitch    = 25;
        camera.distance = 5.5f;
        backend->SetCamera(camera);
        SceneLight sun;
        sun.id             = "sun";
        sun.direction      = {-1, 2, 1};
        sun.intensity      = 4;
        sun.castShadow     = true;
        sun.shadowSoftness = 0;
        SceneLight point;
        point.id             = "point";
        point.type           = LightType::Point;
        point.position       = {-2, 2, 1};
        point.color          = {.2f, .3f, 1};
        point.intensity      = 6;
        point.castShadow     = true;
        point.shadowSoftness = 0;
        SceneLight spot;
        spot.id             = "spot";
        spot.type           = LightType::Spot;
        spot.position       = {2, 3, 2};
        spot.direction      = {-.3f, -1, -.2f};
        spot.intensity      = 8;
        spot.castShadow     = true;
        spot.shadowSoftness = 0;
        spot.color          = {1, .2f, .1f};
        spot.innerConeAngle = .4f;
        spot.outerConeAngle = .8f;
        SceneLight fill     = point;
        fill.id             = "fill";
        fill.castShadow     = false;
        fill.intensity      = .3f;
        SceneLight last     = sun;
        last.id             = "fifth";
        last.direction      = {1, 2, -1};
        last.intensity      = .5f;
        backend->SetLights({sun, point, spot, fill, last});
        settings.shadows = false;
        backend->SetHybrid(settings);
        auto sceneOff    = capture("scene-off");
        settings.shadows = true;
        backend->SetHybrid(settings);
        auto sceneShadow = capture("scene-shadow");
        if (Difference(sceneOff, sceneShadow) < .1)
            throw std::runtime_error("Missing Hybrid shadows");
        for (uint32_t i : {0u, 1u, 2u, 4u})
        {
            settings.view        = HybridView::Shadows;
            settings.shadowLight = i;
            backend->SetHybrid(settings);
            capture("visibility-" + std::to_string(i));
        }
        settings.view = HybridView::Final;
        backend->SetHybrid(settings);
        capture("scene-final");
        AntiAliasingSettings aa;
        aa.method     = AAMethod::TAA;
        bool rejected = false;
        try
        {
            backend->SetAntiAliasing(aa);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected || backend->GetRenderMode() != RenderMode::Hybrid)
            throw std::runtime_error("Hybrid AA validation");
        backend->SetRenderMode(RenderMode::Raster);
        SceneShadowSettings shadow;
        shadow.enabled  = true;
        shadow.softness = 0;
        shadow.samples  = 4;
        backend->SetShadows(shadow);
        auto raster = draw("raster-shadow");
        if (Difference(sceneShadow, raster) > .2)
            throw std::runtime_error("Hybrid/Raster light composition mismatch");
        settings.view = HybridView::AmbientOcclusion;
        backend->SetHybrid(settings);
        auto retainedView = draw("raster-retained-hybrid-view");
        if (Difference(raster, retainedView) > .01)
            throw std::runtime_error("Inactive Hybrid diagnostic changed Raster output");
        settings.view = HybridView::Final;
        backend->SetRenderMode(RenderMode::Hybrid);
        settings.ambientOcclusion = true;
        settings.aoRadius         = 2;
        settings.aoSamples        = 16;
        backend->SetHybrid(settings);
        auto ao = capture("ao-final");
        if (Difference(sceneShadow, ao) < .05)
            throw std::runtime_error("Missing Hybrid AO");
        settings.view = HybridView::AmbientOcclusion;
        backend->SetHybrid(settings);
        capture("ao-visibility");
        settings.view             = HybridView::Final;
        settings.ambientOcclusion = false;
        settings.reflections      = false;
        material.metallic         = .9f;
        material.roughness        = .08f;
        backend->SetMaterial(0, material, "box");
        backend->SetHybrid(settings);
        auto reflectionOff         = capture("reflection-off");
        settings.reflections       = true;
        settings.reflectionSamples = 4;
        backend->SetHybrid(settings);
        auto reflection = capture("reflection-final");
        if (Difference(reflectionOff, reflection) < .05)
            throw std::runtime_error("Missing Hybrid reflection");
        settings.view = HybridView::Reflections;
        backend->SetHybrid(settings);
        capture("reflection-only");
        settings.view                   = HybridView::Final;
        settings.ambientOcclusion       = true;
        settings.aoSamples              = 4;
        settings.reflectionSamples      = 1;
        settings.temporalReconstruction = true;
        backend->SetHybrid(settings);
        settings.historyWeight = 0;
        backend->SetHybrid(settings);
        std::vector<double> reference;
        for (int f = 1; f <= 16; ++f)
        {
            auto raw = capture("temporal-raw-" + std::to_string(f));
            if (reference.empty())
                reference.resize(raw.pixels.size());
            for (size_t i = 0; i < reference.size(); ++i)
                reference[i] += double(raw.pixels[i]) / 16;
        }
        settings.historyWeight = .9f;
        backend->SetHybrid(settings);
        auto  first = capture("temporal-first");
        Image resolved;
        for (int f = 2; f <= 16; ++f)
            resolved = capture("temporal-" + std::to_string(f));
        if (backend->GetHybridHistoryFrames() != 16 || Difference(first, resolved) < .01)
            throw std::runtime_error("Missing Hybrid temporal history");
        auto mse = [&](const Image& im) {
            double sum = 0;
            for (size_t i = 0; i < reference.size(); ++i)
                sum += (im.pixels[i] - reference[i]) * (im.pixels[i] - reference[i]);
            return sum / reference.size();
        };
        std::cout << "Temporal reference MSE: first=" << mse(first) << "; resolved=" << mse(resolved) << '\n';
        if (mse(resolved) >= mse(first))
            throw std::runtime_error("Temporal reconstruction did not reduce reference error");
        camera.yaw += 2;
        backend->SetCamera(camera);
        capture("temporal-motion");
        if (backend->GetHybridHistoryFrames() != 17)
            throw std::runtime_error("Small movement lost history");
        camera.yaw += 40;
        backend->SetCamera(camera);
        auto cut = capture("temporal-cut");
        if (backend->GetHybridHistoryFrames() != 1)
            throw std::runtime_error("Camera cut retained history");
        backend->ResetHybridHistory();
        if (backend->GetHybridHistoryFrames() != 0)
            throw std::runtime_error("Explicit reset failed");
        auto reset = capture("temporal-reset");
        if (Difference(cut, reset) > .01)
            throw std::runtime_error("Reset reused stale history");
        auto full = backend->GetViewCamera();
        full.position[0] += .1f;
        backend->SetViewCamera(full);
        capture("temporal-full-camera");
        if (backend->GetHybridHistoryFrames() != 2)
            throw std::runtime_error("Full camera movement lost history");
        full.verticalFov += 1;
        backend->SetViewCamera(full);
        if (backend->GetHybridHistoryFrames() != 0)
            throw std::runtime_error("Lens change retained history");
        settings.historyWeight = 2;
        bool invalidHybrid     = false;
        try
        {
            backend->SetHybrid(settings);
        }
        catch (const std::invalid_argument&)
        {
            invalidHybrid = true;
        }
        if (!invalidHybrid || backend->GetHybrid().historyWeight != .9f)
            throw std::runtime_error("Invalid Hybrid history mutated settings");
        aa.method = AAMethod::SSAA;
        backend->SetAntiAliasing(aa);
        for (int f = 0; f < 4; ++f)
            capture("temporal-ssaa");
        if (backend->GetHybridHistoryFrames() != 4)
            throw std::runtime_error("Hybrid SSAA history failed");
        aa.method = AAMethod::Off;
        backend->SetAntiAliasing(aa);
        HybridSettings alphaSettings;
        backend->SetHybrid(alphaSettings);
        backend->LoadModel(detail::LoadGLB(assets / "MaterialSample/Layers.glb"));
        backend->SetLighting(LightingMode::Unlit);
        backend->SetCamera(OrbitCamera{});
        auto alphaBase                       = capture("temporal-alpha-base");
        alphaSettings.temporalReconstruction = true;
        backend->SetHybrid(alphaSettings);
        Image alphaResolved;
        for (int f = 0; f < 4; ++f)
            alphaResolved = capture("temporal-alpha");
        if (Difference(alphaBase, alphaResolved) > .05)
            throw std::runtime_error("Temporal OIT changed transparency");
        backend.reset();
        glfwDestroyWindow(window);
        glfwTerminate();
        std::cout << api << " Hybrid shadows, independent lights, GPU presentation and Raster parity passed\n";
        return 0;
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
