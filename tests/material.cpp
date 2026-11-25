#include "Backend.hpp"
#include "TestDevice.hpp"
#include "TestWindowHost.hpp"
#include <algorithm>
#include <chrono>
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
        Require(argc == 3 || argc == 4, "Expected API, asset directory and optional benchmark output");
        std::string           api    = TestAPI(argv[1]);
        std::filesystem::path path   = std::filesystem::path(argv[2]) / "MaterialSample/Dual.glb";
        auto                  source = detail::LoadGLB(path);
        Require(source->materials.size() == 2, "Missing slots");
        Require(
          source->vertices[0].position[3] == 1 && source->vertices[36].position[3] == 2, "Incorrect slot assignment");
        Require(InitTestWindowHost(), "Window host initialization");
        window = OpenTestWindow(api, 320, 240, "Material validation");
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
        if (argc == 4)
        {
            using Clock = std::chrono::steady_clock;
            const std::filesystem::path output = argv[3];
            std::filesystem::create_directories(output);
            backend->SetAntiAliasing({AAMethod::FXAA});
            backend->SetLighting(LightingMode::PBR);
            auto slots = backend->GetMaterials("");
            auto texture = [](bool alternate) {
                Image image{256, 256, std::vector<unsigned char>(256 * 256 * 4)};
                for (uint32_t y = 0; y < image.height; ++y)
                    for (uint32_t x = 0; x < image.width; ++x)
                    {
                        auto* pixel = &image.pixels[(size_t(y) * image.width + x) * 4];
                        const bool check = ((x / 16 + y / 16) % 2) != 0;
                        pixel[0] = alternate ? 32 : (check ? 230 : 125);
                        pixel[1] = alternate ? (check ? 230 : 135) : 45;
                        pixel[2] = check ? 64 : 32;
                        pixel[3] = 255;
                    }
                return image;
            };
            slots[0].baseColor = {1, 1, 1, 1};
            slots[0].baseColorTexture = texture(false);
            backend->SetMaterial(0, slots[0], "");
            for (int i = 0; i < 8; ++i)
                draw();
            auto save = [&](const char* name) {
                const auto& frame = backend->GetRasterAAImage();
                Require(!frame.pixels.empty(), "Benchmark readback was empty");
                const auto file = output / name;
                Require(stbi_write_png(file.string().c_str(), frame.width, frame.height, 4,
                                       frame.pixels.data(), frame.width * 4) != 0, "Capture failed");
            };
            save("before.png");
            const auto before = backend->GetRasterAAImage().pixels;
            auto ms = [](auto start) {
                return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            };
            std::vector<double> frames, updates, visibleUpdates;
            for (int i = 0; i < 31; ++i)
            {
                const auto start = Clock::now();
                draw();
                frames.push_back(ms(start));
            }
            auto replacement = slots[0];
            replacement.baseColorTexture = texture(true);
            for (int i = 0; i < 11; ++i)
            {
                auto start = Clock::now();
                backend->SetMaterial(0, i % 2 ? slots[0] : replacement, "");
                updates.push_back(ms(start));
                draw();
                visibleUpdates.push_back(ms(start));
            }
            Require(backend->GetRasterAAImage().pixels != before, "Replacement did not change the image");
            save("after.png");
            auto median = [](auto values) {
                std::sort(values.begin(), values.end());
                return values[values.size() / 2];
            };
            std::cout << "Frame: " << median(frames) << " ms; SetMaterial: " << median(updates)
                      << " ms; replacement through rendered frame: " << median(visibleUpdates) << " ms\n";
            backend.reset();
            CloseTestWindow(window);
            return 0;
        }
        auto count = [&](int channel, bool left) {
            const auto& im = backend->GetRasterAAImage();
            size_t      n  = 0;
            for (uint32_t y = 0; y < im.height; y++)
                for (uint32_t x = 0; x < im.width; x++)
                {
                    auto* p = &im.pixels[(size_t(y) * im.width + x) * 4];
                    if (
                      (x < im.width / 2) == left && p[channel] > 120 && p[channel] > p[(channel + 1) % 3] * 2
                      && p[channel] > p[(channel + 2) % 3] * 2)
                        n++;
                }
            return n;
        };
        draw();
        backend->SetAntiAliasing({AAMethod::FXAA});
        draw();
        Require(count(0, true) > 500 && count(2, false) > 500, "Multislot colors missing");
        FogSettings fog;
        fog.mode         = FogMode::Linear;
        fog.nearDistance = 0;
        fog.farDistance  = 1;
        fog.color        = {0, 1, 0};
        backend->SetFog(fog);
        draw();
        Require(count(1, true) > 500 && count(1, false) > 500, "Linear fog did not affect both materials");
        fog.mode    = FogMode::ExponentialSquared;
        fog.density = 1;
        backend->SetFog(fog);
        draw();
        Require(count(1, false) > 500, "Exponential fog missing");
        auto badFog        = fog;
        badFog.farDistance = 0;
        bool fogRejected   = false;
        try
        {
            backend->SetFog(badFog);
        }
        catch (const std::invalid_argument&)
        {
            fogRejected = true;
        }
        Require(fogRejected && backend->GetFog().density == 1, "Fog validation not transactional");
        fog.mode         = FogMode::Linear;
        fog.color        = {.2f, .4f, .6f};
        fog.nearDistance = 0;
        fog.farDistance  = 8;
        backend->SetFog(fog);
        draw();
        std::filesystem::create_directories("validation/materials");
        {
            std::ofstream pixels("validation/materials/" + api + "-fog-pixels.bin", std::ios::binary);
            const auto&   im = backend->GetRasterAAImage();
            for (float x : {.33f, .67f})
            {
                const size_t at = (size_t(im.height / 2) * im.width + uint32_t(im.width * x)) * 4;
                pixels.write(reinterpret_cast<const char*>(im.pixels.data() + at), 3);
            }
        }
        backend->SetFog({});
        draw();
        Require(count(0, true) > 500 && count(2, false) > 500, "Fog off did not restore materials");
        auto slots         = backend->GetMaterials("");
        slots[0].baseColor = {0, 1, 0, 1};
        slots[0].roughness = .2f;
        slots[0].metallic  = .8f;
        backend->SetMaterial(0, slots[0], "");
        draw();
        Require(count(1, true) > 500 && count(2, false) > 500, "Slot update leaked");
        Require(backend->GetMaterials("")[0].metallic == .8f, "Factor not retained");
        auto invalid      = slots[0];
        invalid.roughness = -1;
        bool rejected     = false;
        try
        {
            backend->SetMaterial(0, invalid, "");
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        Require(rejected, "Invalid factor accepted");
        draw();
        Require(count(1, true) > 500, "Failed update corrupted state");
        slots[0].baseColor            = {1, 1, 1, 1};
        const auto          imageFile = std::filesystem::temp_directory_path() / ("nova-material-" + api + ".png");
        const unsigned char pixel[]   = {255, 0, 0, 255};
        Require(stbi_write_png(imageFile.string().c_str(), 1, 1, 4, pixel, 4) != 0, "PNG fixture write failed");
        slots[0].baseColorTexture = LoadMaterialImage(imageFile);
        std::filesystem::remove(imageFile);
        Require(
          slots[0].baseColorTexture->pixels == std::vector<unsigned char>(pixel, pixel + 4),
          "Image decode changed RGBA bytes");
        backend->SetMaterial(0, slots[0], "");
        draw();
        Require(count(0, true) > 500 && count(2, false) > 500, "Runtime image upload failed");
        if (api == "vulkan" || api == "metal")
        {
            auto greenMap = slots[0];
            greenMap.baseColorTexture = Image{1, 1, {0, 255, 0, 255}};
            auto blueMap = greenMap;
            blueMap.baseColorTexture = Image{1, 1, {0, 0, 255, 255}};
            backend->SetMaterial(0, blueMap, "");
            backend->SetMaterial(0, greenMap, "");
            draw();
            Require(count(1, true) > 500 && count(2, false) > 500, "Coalesced texture updates lost the latest edit");
            backend->SetRenderMode(RenderMode::Hybrid);
            backend->EnableGPUFramePresentation(false);
            draw();
            Require(count(1, true) > 500 && count(2, false) > 500, "Hybrid read a stale material atlas");
            backend->SetRenderMode(RenderMode::Raster);
            backend->SetRasterSettings({});
        }
        slots[0].baseColorTexture.reset();
        backend->SetMaterial(0, slots[0], "");
        draw();
        Require(count(0, true) == 0, "Texture removal failed");
        for (unsigned method = 1; method <= 5; method++)
        {
            backend->SetAntiAliasing({AAMethod(method)});
            draw();
            Require(count(2, false) > 500, "AA lost second slot");
        }
        backend->SetScene({{"dual", path}}, {{"one", "dual"}});
        auto hit = backend->scene.Pick({{-.6f, 0, 4}, {0, 0, -1}});
        Require(bool(hit), "Scene pick missed");
        auto material      = backend->GetMaterials("dual")[1];
        material.baseColor = {0, 1, 0, 1};
        backend->SetMaterial(1, material, "dual");
        draw();
        Require(count(0, true) > 500 && count(1, false) > 500, "Scene material edit failed");
        Require(
          backend->scene.Pick({{-.6f, 0, 4}, {0, 0, -1}})->distance == hit->distance, "Material edit changed picking");
        backend->LoadModel(source);
        backend->SetLighting(LightingMode::PBR);
        backend->SetAntiAliasing({AAMethod::Off});
        auto dielectric = backend->GetMaterials("")[0];
        dielectric.metallic = 0;
        dielectric.specularColorFactor = {2, .5f, .25f};
        backend->SetMaterial(0, dielectric, "");
        for (auto pipeline : {RasterPipeline::Forward, RasterPipeline::Deferred})
        {
            RasterSettings raster;
            raster.pipeline = pipeline;
            backend->SetRasterSettings(raster);
            backend->SetAntiAliasing({AAMethod::FXAA});
            dielectric.specularFactor = 1;
            backend->SetMaterial(0, dielectric, "");
            draw();
            const auto highlighted = backend->GetRasterAAImage().pixels;
            dielectric.specularFactor = 0;
            backend->SetMaterial(0, dielectric, "");
            draw();
            const auto diffuse = backend->GetRasterAAImage().pixels;
            Require(!diffuse.empty() && diffuse.size() == highlighted.size(), "Specular readback unavailable");
            size_t difference = 0;
            for (size_t i = 0; i < diffuse.size(); ++i)
                difference += std::abs(int(diffuse[i]) - int(highlighted[i]));
            Require(difference > 1000, "Specular factor did not affect raster shading");
            dielectric.clearcoatFactor = 1;
            dielectric.clearcoatRoughnessFactor = .18f;
            backend->SetMaterial(0, dielectric, "");
            draw();
            const auto& coated = backend->GetRasterAAImage().pixels;
            Require(coated.size() == diffuse.size(), "Clearcoat readback unavailable");
            dielectric.clearcoatFactor = 0;
            difference = 0;
            for (size_t i = 0; i < diffuse.size(); ++i)
                difference += std::abs(int(diffuse[i]) - int(coated[i]));
            Require(difference > 1000, "Clearcoat factor did not affect raster shading");
        }
        auto twoUV = std::make_shared<detail::ModelData>(*source);
        for (auto& vertex : twoUV->vertices)
        {
            vertex.uv[0]  = .25f;
            vertex.uv[1]  = .5f;
            vertex.uv1[0] = .75f;
            vertex.uv1[1] = .5f;
        }
        backend->LoadModel(twoUV);
        auto ao             = backend->GetMaterials("")[0];
        ao.occlusionTexture = Image{2, 1, {0, 0, 0, 255, 255, 255, 255, 255}};
        for (auto pipeline : {RasterPipeline::Forward, RasterPipeline::Deferred})
        {
            backend->SetRasterSettings({pipeline});
            ao.occlusionTexCoord = 0;
            backend->SetMaterial(0, ao, "");
            draw();
            const auto uv0       = backend->GetRasterAAImage().pixels;
            ao.occlusionTexCoord = 1;
            backend->SetMaterial(0, ao, "");
            draw();
            const auto& uv1 = backend->GetRasterAAImage().pixels;
            Require(!uv0.empty() && uv0.size() == uv1.size(), "AO readback unavailable");
            size_t difference = 0;
            for (size_t i = 0; i < uv0.size(); ++i)
                difference += std::abs(int(uv0[i]) - int(uv1[i]));
            Require(difference > 1000, "AO texture did not sample the second UV set");
        }
        auto invalidAO              = ao;
        invalidAO.occlusionTexCoord = 2;
        rejected                    = false;
        try
        {
            backend->SetMaterial(0, invalidAO, "");
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        Require(rejected && backend->GetMaterials("")[0].occlusionTexCoord == 1, "Invalid AO UV selection accepted");
        const auto assetRoot = std::filesystem::path(argv[2]);
        auto       coatTest  = detail::LoadGLB(assetRoot / "ClearCoatTest/ClearCoatTest.glb");
        backend->LoadModel(coatTest);
        backend->LoadEnvironment(detail::LoadEnvironmentFile(assetRoot / "Environment/studio_small_09.nenv"));
        OrbitCamera coatCamera;
        coatCamera.distance = 2.55f;
        coatCamera.yaw = coatCamera.pitch = 0;
        backend->SetCamera(coatCamera);
        backend->SetAntiAliasing({AAMethod::FXAA});
        backend->SetRasterSettings({RasterPipeline::Deferred});
        const auto       coatMaterials = backend->GetMaterials("");
        const std::array coatMaps      = {
          &Material::clearcoatTexture, &Material::clearcoatRoughnessTexture, &Material::clearcoatNormalTexture};
        draw();
        const auto allMaps = backend->GetRasterAAImage().pixels;
        Require(!allMaps.empty(), "Clearcoat readback unavailable");
        for (auto map : coatMaps)
        {
            size_t count = 0;
            for (size_t slot = 0; slot < coatMaterials.size(); ++slot)
                if (coatMaterials[slot].*map)
                {
                    auto updated = coatMaterials[slot];
                    (updated.*map).reset();
                    backend->SetMaterial(slot, updated, "");
                    ++count;
                }
            Require(count > 0, "Clearcoat map lost on GLB import");
            draw();
            const auto& masked = backend->GetRasterAAImage().pixels;
            Require(masked.size() == allMaps.size(), "Clearcoat readback dimensions changed");
            size_t difference = 0;
            for (size_t i = 0; i < allMaps.size(); ++i)
                difference += std::abs(int(allMaps[i]) - int(masked[i]));
            Require(difference > 1000, "Clearcoat map did not affect deferred shading");
            for (size_t slot = 0; slot < coatMaterials.size(); ++slot)
                if (coatMaterials[slot].*map)
                    backend->SetMaterial(slot, coatMaterials[slot], "");
        }
        backend->LoadEnvironment(nullptr);
        backend->SetViewCamera(camera);
        backend->SetRasterSettings({});
        backend->SetAntiAliasing({AAMethod::Off});
        backend->LoadModel(source);
        if (backend->GetCapabilities().pathTracing)
        {
            backend->SetRenderMode(RenderMode::PathTracing);
            draw();
            Require(backend->GetAccumulatedSamples() > 0, "Multislot ray tracing failed");
            detail::PathSession session;
            session.Capabilities(config.api, config);
            PathTracingSettings settings;
            settings.resolutionScale = 1;
            settings.maxBounces      = 2;
            auto        uniforms     = detail::MakeSceneUniforms(*source, camera, 1.f, LightingMode::PBR);
            const auto& image        = session.Render(source, nullptr, uniforms, settings, {0, 0, 0, 1}, 96, 96);
            size_t      red = 0, blue = 0;
            for (size_t i = 0; i < image.pixels.size(); i += 4)
            {
                auto* p = &image.pixels[i];
                if (p[0] > p[2] * 2 && p[0] > 80)
                    red++;
                if (p[2] > p[0] * 2 && p[2] > 80)
                    blue++;
            }
            Require(red > 100 && blue > 100, "Ray tracer lost independent material slots");
        }
        backend->WaitIdle();
        backend.reset();
        CloseTestWindow(window);
        window = {};
        QuitTestWindowHost();
        std::cout << "PASS material slots, runtime factors/maps, AA, scene picking and ray tracing: " << api << '\n';
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
