#include "Backend.hpp"
#include "TestDevice.hpp"
#include "TestWindowHost.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
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
    render::detail::PresentationWindow window;
    std::cout.setf(std::ios::unitbuf);
    try
    {
        const std::string api = argc > 1 ? TestAPI(argv[1]) : "metal";
        std::ifstream     source(argc > 2 ? argv[2] : "build/reference/hybrid-rendering/scenes.json");
        auto              fixture         = nlohmann::json::parse(source);
        const bool        probeComparison = argc > 3 && std::string_view(argv[3]) == "--probe-comparison";
        const bool bounceBaseline = probeComparison || (argc > 3 && std::string_view(argv[3]) == "--bounce-baseline");
        const bool transport      = fixture.contains("transport");
        const bool effects        = !transport && !bounceBaseline && fixture.contains("hybridEffects");
        RendererConfig config;
        config.api = api == "metal" ? GraphicsAPI::Metal : api == "vulkan" ? GraphicsAPI::Vulkan : GraphicsAPI::D3D12;
        config.useWarp = argc > 1 && TestUseWarp(argv[1]);
        if (!InitTestWindowHost())
            throw std::runtime_error("Window host initialization");
        float scaleX = 1, scaleY = 1;
        TestPixelDensity(&scaleX, &scaleY);
        window = OpenTestWindow(
          api,
          int(fixture["width"].get<float>() / scaleX),
          int(fixture["height"].get<float>() / scaleY),
          "Reference scene validation");
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
        auto dir = std::filesystem::path(
                     transport      ? "build/validation/pica-transport"
                     : bounceBaseline ? "build/validation/hybrid-gi-baseline"
                     : effects      ? "build/validation/hybrid-reference-effects"
                                    : "build/validation/hybrid-reference")
                   / api;
        std::filesystem::create_directories(dir);
        auto draw = [&](const std::string& name) {
            PollTestWindow();
            int w, h;
            TestPixelSize(window, &w, &h);
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


        DirectionalLight light;
        light.direction = fixture["light"]["direction"].get<std::array<float, 3>>();
        light.color     = {1, 1, 1};
        light.intensity = 4;
        backend->SetDirectionalLight(light);
        if (transport)
        {
            const auto& profile = fixture["transport"];
            backend->LoadEnvironment(detail::LoadEnvironmentFile(profile["environment"].get<std::string>()));
            EnvironmentSettings environment;
            environment.intensity = profile["environmentIntensity"];
            backend->SetEnvironment(environment);
            SceneLight sun;
            sun.id = "sun";
            sun.direction = light.direction;
            sun.intensity = fixture["light"]["intensity"];
            sun.castShadow = true;
            sun.shadowSoftness = profile["shadowSoftness"];
            backend->SetLights({sun});
        }
        backend->SetLighting(LightingMode::PBR);
        SceneShadowSettings shadows;
        shadows.enabled  = true;
        shadows.softness = 0;
        shadows.samples  = 1;
        backend->SetShadows(shadows);
        HybridSettings hybrid;
        hybrid.shadowSamples = 1;
        backend->SetHybrid(hybrid);
        RasterSettings raster;
        raster.pipeline = RasterPipeline::Deferred;
        backend->SetRasterSettings(raster);
        auto gpuFrame = [&] {
            PollTestWindow();
            int w, h;
            TestPixelSize(window, &w, &h);
            if (w != fixture["width"].get<int>() || h != fixture["height"].get<int>())
                throw std::runtime_error("Reference framebuffer size mismatch");
            backend->Resize(w, h);
            const auto reads = backend->RasterReadbackCount();
            if (backend->DrawFrame() != FrameResult::Rendered)
                throw std::runtime_error("Skipped Hybrid frame");
            backend->WaitIdle();
            if (backend->RasterReadbackCount() != reads)
                throw std::runtime_error("Automatic Hybrid readback");
            if (backend->ValidationErrorCount())
                throw std::runtime_error("Reference GPU validation error");
        };
        auto captureGPU = [&](const std::string& name) {
            auto image = backend->CaptureRasterFrame();
            stbi_write_png(
              (dir / (name + ".png")).string().c_str(),
              image.width,
              image.height,
              4,
              image.pixels.data(),
              image.width * 4);
            return image;
        };
        nlohmann::json metrics  = nlohmann::json::array();
        size_t         captures = 0;
        double         maximum  = 0;
        for (const auto& scene : fixture["scenes"])
        {
            if (bounceBaseline && scene["id"] != "global-illumination-test")
                continue;
            backend->SetRenderMode(RenderMode::Raster);
            std::vector<SceneAsset>    assets;
            std::vector<SceneInstance> instances;
            for (const auto& a : scene["assets"])
                assets.push_back({a["id"].get<std::string>(), a["path"].get<std::string>(), true});
            for (const auto& i : scene["instances"])
                instances.push_back(
                  {i["id"].get<std::string>(), i["assetId"].get<std::string>(), i["world"].get<Matrix4>()});
            backend->SetScene(assets, instances);
            size_t triangles = 0;
            for (const auto& d : backend->scene.draws)
                triangles += size_t(d.vertexCount / 3) * d.instanceCount;
            if (triangles != scene["triangles"].get<size_t>())
                throw std::runtime_error("Reference triangle count mismatch");
            double sceneAO = 0, sceneReflection = 0;
            for (const auto& c : scene["cameras"])
            {
                Camera camera;
                camera.position    = c["position"].get<std::array<float, 3>>();
                camera.orientation = c["orientation"].get<std::array<float, 4>>();
                camera.verticalFov = c.value("verticalFov", 60.f);
                camera.nearPlane   = 1;
                camera.farPlane    = 1000;
                backend->SetViewCamera(camera);
                std::string name = scene["id"].get<std::string>() + "-" + c["id"].get<std::string>();
                if (transport)
                {
                    backend->SetRenderMode(RenderMode::Hybrid);
                    const auto& options = fixture["transport"];
                    HybridSettings profile;
                    profile.shadows = profile.shadowTemporalReconstruction = true;
                    profile.ambientOcclusion = profile.temporalReconstruction = true;
                    profile.aoRadius = options.value("aoRadius", profile.aoRadius);
                    profile.aoStrength = options.value("aoStrength", profile.aoStrength);
                    profile.halfResolutionAO = options.value("halfResolutionAO", false);
                    profile.reflections = true;
                    profile.probeGI = profile.probeSkyLighting = profile.probeMultiBounce = true;
                    profile.probeSharedRays = profile.probeOctahedral = true;
                    backend->SetHybrid(profile);
                    for (int frame = 0; frame < 32; ++frame)
                        gpuFrame();
                    captureGPU(name);
                    ++captures;
                    if (options.value("motion", false))
                    {
                        auto timedFrames = [&](bool move) {
                            std::array<double, 16> samples;
                            for (size_t frame = 0; frame < samples.size(); ++frame)
                            {
                                if (move)
                                {
                                    auto moving = camera;
                                    moving.position[0] += .045f * float(frame + 1);
                                    backend->SetViewCamera(moving);
                                }
                                const auto start = std::chrono::steady_clock::now();
                                gpuFrame();
                                samples[frame] = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - start).count();
                            }
                            std::sort(samples.begin(), samples.end());
                            return (samples[7] + samples[8]) / 2;
                        };
                        const double still = timedFrames(false), moving = timedFrames(true);
                        captureGPU(name + "-motion");
                        ++captures;
                        std::cout << name << ": still " << still << " ms, moving " << moving << " ms\n";
                    }
                    continue;
                }
                if (bounceBaseline)
                {
                    backend->SetRenderMode(RenderMode::Hybrid);
                    nlohmann::json result{{"camera", c["id"]}};
                    for (int mode = 0; mode < (probeComparison ? 3 : 2); ++mode)
                    {
                        const std::string label = mode == 2 ? "probes" : mode == 1 ? "pixel-bounce" : "off";
                        HybridSettings    profile;
                        profile.shadowSamples = 1;
                        profile.diffuseBounce = mode == 1;
                        profile.probeGI       = mode == 2;
                        backend->SetHybrid(profile);
                        for (int n = 0; n < (profile.probeGI ? 52 : 4); ++n)
                            gpuFrame();
                        std::array<double, 15> samples;
                        for (auto& sample : samples)
                        {
                            const auto start = std::chrono::steady_clock::now();
                            gpuFrame();
                            sample = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                                       .count();
                        }
                        std::sort(samples.begin(), samples.end());
                        result[label] = samples[samples.size() / 2];
                        captureGPU(name + "-" + label);
                        for (auto& sample : samples)
                        {
                            const auto start = std::chrono::steady_clock::now();
                            backend->RenderHybridOffscreen(
                              fixture["width"].get<uint32_t>(), fixture["height"].get<uint32_t>());
                            sample = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                                       .count();
                        }
                        std::sort(samples.begin(), samples.end());
                        result["renderOnlyMs"][label] = samples[samples.size() / 2];
                        if (backend->ValidationErrorCount())
                            throw std::runtime_error("Reference offscreen GPU validation error");
                        ++captures;
                    }
                    metrics.push_back(result);
                    continue;
                }
                backend->SetRenderMode(RenderMode::Raster);
                hybrid               = HybridSettings{};
                hybrid.shadowSamples = 1;
                hybrid.view          = HybridView::Final;
                backend->SetHybrid(hybrid);
                auto baseline = draw(name + "-raster");
                backend->SetRenderMode(RenderMode::Hybrid);
                PollTestWindow();
                int w, h;
                TestPixelSize(window, &w, &h);
                backend->Resize(w, h);
                auto before = backend->RasterReadbackCount();
                backend->DrawFrame();
                backend->WaitIdle();
                if (backend->RasterReadbackCount() != before)
                    throw std::runtime_error("Reference Hybrid readback");
                auto image = backend->CaptureRasterFrame();
                stbi_write_png(
                  (dir / (name + "-hybrid.png")).string().c_str(),
                  image.width,
                  image.height,
                  4,
                  image.pixels.data(),
                  image.width * 4);
                auto difference = Difference(image, baseline);
                maximum         = std::max(maximum, difference);
                if (difference > 1)
                    throw std::runtime_error(
                      "Reference direct-light parity " + name + ": " + std::to_string(difference));
                if (backend->ValidationErrorCount())
                    throw std::runtime_error("Reference GPU validation error");
                if (effects)
                {
                    const auto& settings    = fixture["hybridEffects"];
                    hybrid.ambientOcclusion = true;
                    hybrid.aoRadius         = settings["aoRadius"];
                    hybrid.aoSamples        = settings["aoSamples"];
                    backend->SetHybrid(hybrid);
                    gpuFrame();
                    auto ao     = captureGPU(name + "-ao");
                    hybrid.view = HybridView::AmbientOcclusion;
                    backend->SetHybrid(hybrid);
                    gpuFrame();
                    captureGPU(name + "-ao-visibility");
                    hybrid.view              = HybridView::Final;
                    hybrid.ambientOcclusion  = false;
                    hybrid.reflections       = true;
                    hybrid.reflectionSamples = settings["reflectionSamples"];
                    backend->SetHybrid(hybrid);
                    gpuFrame();
                    auto reflection = captureGPU(name + "-reflections");
                    hybrid.view     = HybridView::Reflections;
                    backend->SetHybrid(hybrid);
                    gpuFrame();
                    captureGPU(name + "-reflection-visibility");
                    hybrid.view             = HybridView::Final;
                    hybrid.ambientOcclusion = true;
                    backend->SetHybrid(hybrid);
                    gpuFrame();
                    captureGPU(name + "-combined");
                    hybrid.temporalReconstruction = true;
                    backend->SetHybrid(hybrid);
                    gpuFrame();
                    auto      first  = captureGPU(name + "-temporal-first");
                    const int frames = settings["historyFrames"];
                    for (int f = 1; f < frames; ++f)
                        gpuFrame();
                    auto resolved = captureGPU(name + "-temporal-resolved");
                    if (backend->GetHybridHistoryFrames() != unsigned(frames))
                        throw std::runtime_error("Reference temporal history failed");
                    nlohmann::json metric = {
                      {"scene", scene["id"]},
                      {"camera", c["id"]},
                      {"aoDifference", Difference(image, ao)},
                      {"reflectionDifference", Difference(image, reflection)},
                      {"temporalDifference", Difference(first, resolved)}};
                    sceneAO         = std::max(sceneAO, Difference(image, ao));
                    sceneReflection = std::max(sceneReflection, Difference(image, reflection));
                    if (c["id"] == "angle-1")
                    {
                        hybrid.historyWeight = 0;
                        backend->SetHybrid(hybrid);
                        std::vector<double> reference(first.pixels.size());
                        for (int f = 0; f < frames; ++f)
                        {
                            gpuFrame();
                            auto raw = backend->CaptureRasterFrame();
                            for (size_t i = 0; i < reference.size(); ++i)
                                reference[i] += double(raw.pixels[i]) / frames;
                        }
                        auto mse = [&](const Image& im) {
                            double sum = 0;
                            for (size_t i = 0; i < reference.size(); ++i)
                                sum += std::pow(im.pixels[i] - reference[i], 2);
                            return sum / reference.size();
                        };
                        metric["firstMSE"]    = mse(first);
                        metric["resolvedMSE"] = mse(resolved);
                        if (mse(first) > .01 && mse(resolved) >= mse(first))
                            throw std::runtime_error("Reference temporal error did not improve: " + name);
                    }
                    metrics.push_back(metric);
                    std::cout << metric.dump() << '\n';
                    captures += 7;
                }
                captures += 2;
                std::cout << name << ": " << triangles << " triangles, MAE " << difference << "/255\n";
            }
            if (effects && (sceneAO < .001 || sceneReflection < .001))
                throw std::runtime_error("Missing scene effect: " + scene["id"].get<std::string>());
        }
        if (effects || bounceBaseline)
        {
            std::ofstream output(dir / "metrics.json");
            output << metrics.dump(2) << '\n';
        }
        backend.reset();
        CloseTestWindow(window);
        QuitTestWindowHost();
        std::cout << api << " reference captures=" << captures;
        if (!transport)
            std::cout << ", max Raster/Hybrid MAE=" << maximum << "/255";
        std::cout << '\n';
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
