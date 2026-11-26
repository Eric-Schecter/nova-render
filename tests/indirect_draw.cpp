#include "Backend.hpp"
#include "TestWindowHost.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stb_image_write.h>
#include <stdexcept>

int main(int argc, char** argv)
{
    render::detail::PresentationWindow surface;
    try
    {
        if (argc != 2)
            throw std::invalid_argument("Expected output directory");
        if (!InitTestWindowHost())
            throw std::runtime_error("SDL video initialization failed");
        surface = OpenTestWindow("vulkan", 640, 360, "Nova indirect draw benchmark");
        render::RendererConfig config;
        config.api              = render::GraphicsAPI::Vulkan;
        config.enableValidation = !std::getenv("NOVA_VULKAN_BENCH_NO_VALIDATION");
        auto backend            = render::detail::CreateVulkanBackend(surface, config);
        int  width, height;
        TestPixelSize(surface, &width, &height);
        backend->Resize(width, height);
        backend->SetAntiAliasing({render::AAMethod::Off});
        const std::filesystem::path output = argv[1];
        std::filesystem::create_directories(output);

        for (const unsigned groups : {16u, 256u})
            for (const unsigned count : {10'000u, 50'000u, 125'000u})
            {
                std::vector<render::SceneAsset> assets;
                for (unsigned group = 0; group < groups; ++group)
                {
                    auto geometry      = std::make_shared<render::SceneGeometry>();
                    geometry->vertices = {
                      {{-.45f, 0, 0}, {0, 0, 1}},
                      {{.45f, 0, 0}, {0, 0, 1}},
                      {{.45f, .8f, 0}, {0, 0, 1}},
                      {{-.45f, 0, 0}, {0, 0, 1}},
                      {{.45f, .8f, 0}, {0, 0, 1}},
                      {{-.45f, .8f, 0}, {0, 0, 1}}};
                    render::Material material;
                    material.baseColor = {.1f + float(group % 5) * .15f, .35f, .2f + float(group % 7) * .1f, 1};
                    geometry->materials.push_back(material);
                    render::SceneAsset asset;
                    asset.id          = std::to_string(group);
                    asset.sourceSpace = true;
                    asset.geometry    = std::move(geometry);
                    assets.push_back(std::move(asset));
                }
                std::vector<render::SceneInstance> instances;
                instances.reserve(count);
                for (unsigned i = 0; i < count; ++i)
                {
                    render::SceneInstance instance{std::to_string(i), assets[i % groups].id};
                    instance.world[12]  = float(i % 128) * 1.2f - 75;
                    instance.world[13]  = float((i / 128) % 32) * 1.2f - 18;
                    instance.world[14]  = -float(i / 4096) * 1.5f;
                    instance.castShadow = false;
                    instances.push_back(std::move(instance));
                }
                backend->SetScene(assets, instances);
                backend->SetAntiAliasing({render::AAMethod::Off});
                std::cout << "Benchmark " << groups << " " << count << std::endl;
                std::vector<double> times;
                for (int i = 0; i < 26; ++i)
                {
                    PollTestWindow();
                    render::Camera camera;
                    camera.position = {float(i % 8) * 2 - 7, 0, 28};
                    camera.farPlane = 160;
                    backend->SetViewCamera(camera);
                    const auto start = std::chrono::steady_clock::now();
                    if (backend->DrawFrame() != render::FrameResult::Rendered)
                        throw std::runtime_error("Benchmark frame skipped");
                    backend->WaitIdle();
                    if (i >= 6)
                        times.push_back(
                          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
                }
                std::sort(times.begin(), times.end());
                backend->SetAntiAliasing({render::AAMethod::FXAA});
                if (backend->DrawFrame() != render::FrameResult::Rendered)
                    throw std::runtime_error("Benchmark capture frame skipped");
                backend->WaitIdle();
                const auto& image = backend->GetRasterAAImage();
                const auto  name  = std::to_string(groups) + "-" + std::to_string(count) + ".png";
                if (
                  image.pixels.empty()
                  || !stbi_write_png(
                    (output / name).string().c_str(),
                    image.width,
                    image.height,
                    4,
                    image.pixels.data(),
                    image.width * 4))
                    throw std::runtime_error("Benchmark frame readback failed");
                std::cout << groups << " groups, " << count << " instances, median " << times[times.size() / 2]
                          << " ms, p95 " << times[times.size() * 95 / 100 - 1] << " ms" << std::endl;
            }
        if (backend->ValidationErrorCount())
            throw std::runtime_error("Vulkan validation failed");
        backend.reset();
        CloseTestWindow(surface);
        QuitTestWindowHost();
    }
    catch (const std::exception& error)
    {
        if (surface)
            CloseTestWindow(surface);
        QuitTestWindowHost();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
