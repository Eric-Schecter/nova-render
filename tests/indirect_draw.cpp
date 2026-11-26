#include "Backend.hpp"
#include "TestWindowHost.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stb_image_write.h>
#include <stdexcept>
#include <string_view>

namespace
{
    struct Workload
    {
        unsigned groups, count, rings = 0, sectors = 0, visibility = 0;
    };

    std::vector<render::SceneVertex> Sphere(unsigned rings, unsigned sectors)
    {
        constexpr float pi     = 3.14159265358979323846f;
        auto            vertex = [&](unsigned ring, unsigned sector) {
            const float         latitude  = pi * (float(ring) / rings - .5f);
            const float         longitude = sector == sectors ? 0.f : 2 * pi * float(sector) / sectors;
            const float         radius    = (ring == 0 || ring == rings) ? 0.f : std::cos(latitude);
            render::SceneVertex v;
            v.normal = {radius * std::cos(longitude), std::sin(latitude), radius * std::sin(longitude)};
            for (size_t axis = 0; axis < 3; ++axis)
                v.position[axis] = .11f * v.normal[axis];
            return v;
        };

        std::vector<render::SceneVertex> vertices;
        vertices.reserve(6 * (rings - 1) * sectors);
        for (unsigned ring = 0; ring < rings; ++ring)
            for (unsigned sector = 0; sector < sectors; ++sector)
            {
                const auto a = vertex(ring, sector);
                const auto b = vertex(ring + 1, sector);
                const auto c = vertex(ring + 1, sector + 1);
                const auto d = vertex(ring, sector + 1);
                if (ring + 1 != rings)
                    vertices.insert(vertices.end(), {a, b, c});
                if (ring != 0)
                    vertices.insert(vertices.end(), {a, c, d});
            }
        return vertices;
    }
} // namespace

int main(int argc, char** argv)
{
    render::detail::PresentationWindow surface;
    try
    {
        bool             heavy = false;
        std::string_view api   = "vulkan";
        if (argc < 2)
            throw std::invalid_argument("Usage: nova_indirect_draw_benchmark <output directory> [--heavy] [--metal]");
        for (int i = 2; i < argc; ++i)
        {
            const std::string_view option = argv[i];
            if (option == "--heavy")
                heavy = true;
            else if (option == "--metal")
                api = "metal";
            else
                throw std::invalid_argument("Unknown indirect draw benchmark option");
        }
        if (!InitTestWindowHost())
            throw std::runtime_error("SDL video initialization failed");
        surface = OpenTestWindow(api, 640, 360, "Nova indirect draw benchmark");
        render::RendererConfig config;
        config.api              = api == "metal" ? render::GraphicsAPI::Metal : render::GraphicsAPI::Vulkan;
        config.enableValidation = !std::getenv("NOVA_VULKAN_BENCH_NO_VALIDATION");
        std::unique_ptr<render::detail::Backend> backend;
#ifdef __APPLE__
        if (config.api == render::GraphicsAPI::Metal)
            backend = render::detail::CreateMetalBackend(surface, config);
#endif
#ifdef NOVA_HAS_VULKAN
        if (config.api == render::GraphicsAPI::Vulkan)
            backend = render::detail::CreateVulkanBackend(surface, config);
#endif
        if (!backend)
            throw std::runtime_error("Requested graphics backend is unavailable");
        int width, height;
        TestPixelSize(surface, &width, &height);
        backend->Resize(width, height);
        backend->SetAntiAliasing({render::AAMethod::Off});
        const std::filesystem::path output = argv[1];
        std::filesystem::create_directories(output);

        std::vector<Workload> workloads;
        if (heavy)
        {
            // Move a deterministic fraction outside the frustum without changing the submitted workload.
            for (const unsigned visibility : {10u, 50u, 90u})
            {
                workloads.push_back({64, 4096, 12, 16, visibility});
                workloads.push_back({64, 4096, 24, 32, visibility});
            }
        }
        else
        {
            for (const unsigned groups : {16u, 256u})
                for (const unsigned count : {10'000u, 50'000u, 125'000u})
                    workloads.push_back({groups, count});
        }
        for (const auto& [groups, count, rings, sectors, visibility] : workloads)
        {
            const auto sphere = heavy ? Sphere(rings, sectors) : std::vector<render::SceneVertex>{};
            std::vector<render::SceneAsset> assets;
            for (unsigned group = 0; group < groups; ++group)
            {
                auto geometry      = std::make_shared<render::SceneGeometry>();
                geometry->vertices = heavy ? sphere
                                           : std::vector<render::SceneVertex>{
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
                instance.world[12] = float(i % 128) * 1.2f - 75;
                instance.world[13] = float((i / 128) % 32) * 1.2f - 18;
                instance.world[14] = -float(i / 4096) * 1.5f;
                if (heavy)
                {
                    instance.world[12] = (float(i % 64) - 31.5f) * .31f;
                    instance.world[13] = (float(i / 64) - 31.5f) * .27f;
                    // The multiplier permutes all 4096 slots so coverage is spread across the grid.
                    if ((i * 1543u) % count >= count * visibility / 100)
                        instance.world[12] += 200;
                }
                instance.castShadow = false;
                instances.push_back(std::move(instance));
            }
            backend->SetScene(assets, instances);
            backend->SetAntiAliasing({render::AAMethod::Off});
            const auto name = heavy ? std::to_string(sphere.size() / 3) + "-tri-" + std::to_string(visibility)
                                    : std::to_string(groups) + "-" + std::to_string(count);
            std::cout << "Benchmark " << name << " (" << groups << " groups, " << count << " instances)" << std::endl;
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

            if (
              image.pixels.empty()
              || !stbi_write_png(
                (output / (name + ".png")).string().c_str(),
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
            throw std::runtime_error("Graphics validation failed");
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
