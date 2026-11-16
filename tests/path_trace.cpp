#include "TestDevice.hpp"
#include "path/PathTracing.hpp"
#include <filesystem>
#include <iostream>
#include <set>
#include <stb_image_write.h>
#include <stdexcept>
int main(int argc, char** argv)
{
    try
    {
        if (argc != 4)
            throw std::runtime_error("Expected API, assets directory and screenshot directory");
        const std::string      api = TestAPI(argv[1]);
        render::RendererConfig config;
        if (api == "metal")
            config.api = render::GraphicsAPI::Metal;
        else if (api == "vulkan")
            config.api = render::GraphicsAPI::Vulkan;
        else if (api == "dx12")
        {
            config.api     = render::GraphicsAPI::D3D12;
            config.useWarp = TestUseWarp(argv[1]);
        }
        else
            throw std::runtime_error("Unknown API");
        render::detail::PathSession session;
        const auto                  caps = session.Capabilities(config.api, config);
        std::cout << "Path device: " << caps.rayTracingDevice << "; software=" << caps.softwareRayTracing << std::endl;
        if (std::string(argv[1]) == "dx12-hardware" && (!caps.pathTracing || caps.softwareRayTracing))
            throw std::runtime_error("Explicit hardware test requires hardware ray tracing");
        if (!caps.pathTracing)
        {
            std::cout << "SKIP: " << caps.reason << '\n';
            return 77;
        }
        auto triangle      = std::make_shared<render::detail::ModelData>(*render::detail::EmptyModelData());
        triangle->vertices = {
          {{-1, -1, 0, 1}, {0, 0, 1, 0}, {0, 0, 0, 0}},
          {{1, -1, 0, 1}, {0, 0, 1, 0}, {1, 0, 0, 0}},
          {{0, 1, 0, 1}, {0, 0, 1, 0}, {.5f, 1, 0, 0}}};
        triangle->images[0].pixels = {255, 0, 0, 255};
        triangle->material         = {0, 1, 1, 1};
        render::DirectionalLight light;
        light.direction = {0, 0, 1};
        light.intensity = 3;
        render::EnvironmentSettings environment;
        environment.showBackground = false;
        render::OrbitCamera         camera;
        render::PathTracingSettings settings;
        settings.resolutionScale = 1;
        settings.maxBounces      = 2;
        auto uniforms            = render::detail::MakeSceneUniforms(
          *triangle, camera, 1, render::LightingMode::PBR, light, nullptr, environment);
        const auto&  red    = session.Render(triangle, nullptr, uniforms, settings, {0, 0, 0, 1}, 64, 64);
        const size_t center = (32 * 64 + 32) * 4;
        if (
          red.pixels[center] <= red.pixels[center + 1] + 32 || red.pixels[center + 3] != 255 || session.Samples() != 1)
            throw std::runtime_error("Ray-traced triangle did not shade red");
        session.Render(triangle, nullptr, uniforms, settings, {0, 0, 0, 1}, 64, 64);
        if (session.Samples() != 2)
            throw std::runtime_error("Accumulation did not advance");
        camera.yaw = 15;
        uniforms   = render::detail::MakeSceneUniforms(
          *triangle, camera, 1, render::LightingMode::PBR, light, nullptr, environment);
        session.Render(triangle, nullptr, uniforms, settings, {0, 0, 0, 1}, 64, 64);
        if (session.Samples() != 1)
            throw std::runtime_error("Camera change did not reset accumulation");
        settings.samplesPerFrame = 2;
        session.Render(triangle, nullptr, uniforms, settings, {0, 0, 0, 1}, 64, 64);
        if (session.Samples() != 2)
            throw std::runtime_error("Sample-setting change did not reset accumulation");
        session.Reset();
        if (session.Samples() != 0)
            throw std::runtime_error("Manual reset failed");
        const std::filesystem::path assets = argv[2];
        auto                        model  = render::detail::LoadGLB(assets / "DamagedHelmet/DamagedHelmet.glb");
        auto sky                   = render::detail::LoadEnvironmentFile(assets / "Environment/venice_sunset.nenv");
        camera.yaw                 = 20;
        camera.pitch               = 5;
        light.intensity            = 0;
        environment.showBackground = true;
        uniforms                   = render::detail::MakeSceneUniforms(
          *model, camera, 4.0f / 3, render::LightingMode::PBR, light, sky.get(), environment);
        settings.resolutionScale = 1;
        settings.samplesPerFrame = 2;
        settings.maxBounces      = 3;
        std::vector<unsigned char> early, middle;
        for (int i = 0; i < 16; ++i)
        {
            const auto& frame = session.Render(model, sky, uniforms, settings, {0, 0, 0, 1}, 320, 240);
            if (i == 0)
                early = frame.pixels;
            if (i == 3)
                middle = frame.pixels;
        }
        const auto image = session.Render(model, sky, uniforms, settings, {0, 0, 0, 1}, 320, 240);
        std::filesystem::create_directories(argv[3]);
        const auto path = std::filesystem::path(argv[3]) / ("path-" + api + ".png");
        if (!stbi_write_png(path.string().c_str(), image.width, image.height, 4, image.pixels.data(), image.width * 4))
            throw std::runtime_error("PNG write failed");
        auto mse = [&](const std::vector<unsigned char>& pixels) {
            double total = 0;
            size_t count = 0;
            for (uint32_t y = 48; y < 192; ++y)
                for (uint32_t x = 80; x < 240; ++x)
                    for (int k = 0; k < 3; ++k)
                    {
                        const size_t i = (size_t(y) * 320 + x) * 4 + k;
                        const double d = double(pixels[i]) - image.pixels[i];
                        total += d * d;
                        ++count;
                    }
            return total / count;
        };
        const auto earlyError = mse(early), middleError = mse(middle);
        if (!(middleError < earlyError))
            throw std::runtime_error("Progressive image did not converge toward reference");
        std::cout << "Convergence MSE: 2spp=" << earlyError << "; 8spp=" << middleError << "; reference=34spp\n";
        if (session.Samples() != 34)
            throw std::runtime_error("Model accumulation did not advance");
        settings.denoise         = true;
        const auto filtered      = session.Render(model, sky, uniforms, settings, {0, 0, 0, 1}, 320, 240);
        const auto filteredError = mse(filtered.pixels);
        std::cout << "Denoise MSE: raw=" << earlyError << "; filtered=" << filteredError << '\n';
        if (!(filteredError < earlyError) || session.Samples() != 2)
            throw std::runtime_error("Denoise did not reduce reference error or reset samples");
        for (size_t i = 0; i < 4; ++i)
            if (filtered.pixels[i] != early[i])
                throw std::runtime_error("Denoise modified sky background");
        const auto filteredPath = std::filesystem::path(argv[3]) / ("denoise-" + api + ".png");
        stbi_write_png(filteredPath.string().c_str(), 320, 240, 4, filtered.pixels.data(), 320 * 4);
        const auto rawPath = std::filesystem::path(argv[3]) / ("denoise-raw-" + api + ".png");
        stbi_write_png(rawPath.string().c_str(), 320, 240, 4, early.data(), 320 * 4);
        settings.denoise    = false;
        const auto restored = session.Render(model, sky, uniforms, settings, {0, 0, 0, 1}, 320, 240);
        if (restored.pixels != early)
            throw std::runtime_error("Disabling denoise did not restore raw output");
        auto cornell = render::detail::LoadGLB(assets / "CornellBox/CornellBox.glb");
        if (cornell->vertices.size() != 96)
            throw std::runtime_error("Cornell triangle count");
        camera.yaw      = 0;
        camera.pitch    = 0;
        light.direction = {0, 1, 2};
        light.intensity = 3;
        uniforms        = render::detail::MakeSceneUniforms(
          *cornell, camera, 1, render::LightingMode::PBR, light, nullptr, environment);
        settings.samplesPerFrame = 4;
        settings.maxBounces      = 5;
        settings.denoise         = true;
        for (int i = 0; i < 8; ++i)
            session.Render(cornell, nullptr, uniforms, settings, {.025f, .035f, .055f, 1}, 256, 256);
        const auto cornellImage =
          session.Render(cornell, nullptr, uniforms, settings, {.025f, .035f, .055f, 1}, 256, 256);
        size_t redCount = 0, greenCount = 0;
        for (size_t i = 0; i < cornellImage.pixels.size(); i += 4)
        {
            const auto r = cornellImage.pixels[i], g = cornellImage.pixels[i + 1], b = cornellImage.pixels[i + 2];
            if (r > g * 1.6 && r > b * 1.6 && r > 40)
                ++redCount;
            if (g > r * 1.6 && g > b * 1.3 && g > 40)
                ++greenCount;
        }
        if (redCount < 500 || greenCount < 500)
            throw std::runtime_error("Cornell red/green walls missing");
        const auto cornellPath = std::filesystem::path(argv[3]) / ("cornell-" + api + ".png");
        stbi_write_png(cornellPath.string().c_str(), 256, 256, 4, cornellImage.pixels.data(), 256 * 4);
        std::cout << "PASS red triangle pixels, accumulation, automatic/manual reset and textured HDR model at "
                  << session.Samples() << " samples\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
