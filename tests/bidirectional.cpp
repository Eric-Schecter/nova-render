#include "path/PathTracing.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace render;
using namespace render::detail;
int main(int argc, char** argv)
{
    try
    {
        if (argc != 4)
            throw std::runtime_error("Expected API, assets and output directory");
        RendererConfig    config;
        const std::string api = argv[1];
        config.api = api == "metal" ? GraphicsAPI::Metal : api == "vulkan" ? GraphicsAPI::Vulkan : GraphicsAPI::D3D12;
        config.useWarp = api == "dx12";
        PathSession session(true);
        const auto  caps = session.Capabilities(config.api, config);
        if (!caps.pathTracing)
        {
            std::cout << "SKIP " << caps.reason << '\n';
            return 77;
        }
        const uint32_t size  = caps.softwareRayTracing ? 24 : 48;
        auto           model = LoadGLB(std::filesystem::path(argv[2]) / "CornellBox/CornellBox.glb");
        OrbitCamera    camera;
        camera.yaw      = 0;
        camera.pitch    = 0;
        camera.distance = 3.6f;
        DirectionalLight light;
        light.intensity = 0;
        EnvironmentSettings environment;
        environment.intensity      = 0;
        environment.showBackground = false;
        auto uniforms = MakeSceneUniforms(*model, camera, 1, LightingMode::PBR, light, nullptr, environment);
        PathTracingSettings settings;
        settings.samplesPerFrame = 4;
        settings.resolutionScale = 1;
        settings.maxBounces      = 5;
        auto render              = [&](PathIntegrator integrator, int spp) {
            settings.integrator = integrator;
            session.Reset();
            std::vector<float> pixels;
            const auto         start = std::chrono::steady_clock::now();
            for (int i = 0; i < spp; i += 4)
            {
                const auto& image = session.Render(model, nullptr, uniforms, settings, {0, 0, 0, 1}, size, size);
                if (i + 4 == spp)
                {
                    pixels.resize(size * size);
                    std::memcpy(pixels.data(), image.pixels.data(), image.pixels.size());
                }
            }
            for (float x : pixels)
                if (!std::isfinite(x) || x < 0)
                    throw std::runtime_error("Invalid BDPT radiance");
            const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::cout << (integrator == PathIntegrator::Bidirectional ? "BDPT" : "PT+MIS") << " spp=" << spp
                      << " seconds=" << seconds << std::endl;
            return pixels;
        };
        render(PathIntegrator::PathTracing, 4);
        render(PathIntegrator::Bidirectional, 4);
        const auto pt8 = render(PathIntegrator::PathTracing, 8), bdpt8 = render(PathIntegrator::Bidirectional, 8),
                   pt32 = render(PathIntegrator::PathTracing, 32), bdpt32 = render(PathIntegrator::Bidirectional, 32),
                   pt128   = render(PathIntegrator::PathTracing, 128),
                   bdpt128 = render(PathIntegrator::Bidirectional, 128),
                   ref     = render(PathIntegrator::PathTracing, 2048),
                   bdptRef = render(PathIntegrator::Bidirectional, 2048);
        auto metric        = [&](const auto& pixels, bool error) {
            double total = 0;
            size_t count = 0;
            for (uint32_t y = size / 4; y < size * 9 / 10; y++)
                for (uint32_t x = size / 5; x < size * 4 / 5; x++)
                {
                    const auto i = y * size + x;
                    total += error ? std::pow(pixels[i] - ref[i], 2) : pixels[i];
                    count++;
                }
            return total / count;
        };
        const double energy = std::abs(metric(bdptRef, false) / metric(ref, false) - 1);
        std::cout << "Cornell linear MSE: PT8=" << metric(pt8, true) << " BDPT8=" << metric(bdpt8, true)
                  << " PT32=" << metric(pt32, true) << " BDPT32=" << metric(bdpt32, true)
                  << " PT128=" << metric(pt128, true) << " BDPT128=" << metric(bdpt128, true)
                  << " high-spp energy difference=" << energy << std::endl;
        if (
          energy > .08 || !(metric(bdpt128, true) < metric(bdpt32, true))
          || !(metric(bdpt8, true) < metric(pt8, true) && metric(bdpt32, true) < metric(pt32, true)))
            throw std::runtime_error("BDPT energy/convergence failed");
        // Check the direct-only limit and a shorter full path length as independent energy audits.
        for (uint32_t bounces : {1u, 3u, 8u})
        {
            settings.maxBounces = bounces;
            const int    spp    = bounces == 8 ? 256 : 1024;
            const auto   a = render(PathIntegrator::PathTracing, spp), b = render(PathIntegrator::Bidirectional, spp);
            const double difference = std::abs(metric(b, false) / metric(a, false) - 1);
            std::cout << "Depth=" << bounces << " energy difference=" << difference << std::endl;
            if (difference > .08)
                throw std::runtime_error("BDPT path length/density audit failed");
        }
        auto mapped             = std::make_shared<ModelData>(*model);
        mapped->images[1].width = mapped->images[1].height = 1;
        mapped->images[1].pixels                           = {160, 128, 247, 255};
        for (size_t i = 0; i < mapped->vertices.size(); i += 3)
        {
            const float u = mapped->vertices[i].uv[0], v = mapped->vertices[i].uv[1];
            mapped->vertices[i].uv[0]     = u - .005f;
            mapped->vertices[i].uv[1]     = v - .005f;
            mapped->vertices[i + 1].uv[0] = u + .005f;
            mapped->vertices[i + 1].uv[1] = v - .005f;
            mapped->vertices[i + 2].uv[0] = u;
            mapped->vertices[i + 2].uv[1] = v + .005f;
        }
        model               = mapped;
        settings.maxBounces = 3;
        uniforms            = MakeSceneUniforms(*model, camera, 1, LightingMode::PBR, light, nullptr, environment);
        auto mappedPT       = render(PathIntegrator::PathTracing, 1024),
             mappedBDPT     = render(PathIntegrator::Bidirectional, 1024);
        const double mappedEnergy = std::abs(metric(mappedBDPT, false) / metric(mappedPT, false) - 1);
        std::cout << "Adjoint normal-map energy difference=" << mappedEnergy << std::endl;
        if (mappedEnergy > .1)
            throw std::runtime_error("Adjoint normal-map energy failed");
        // Without finite emitters, BDPT must preserve the existing environment/delta estimator exactly.
        auto nonEmissive      = std::make_shared<ModelData>(*model);
        nonEmissive->emissive = {0, 0, 0, 0};
        model                 = nonEmissive;
        environment.intensity = 1;
        light.intensity       = 1;
        uniforms              = MakeSceneUniforms(*model, camera, 1, LightingMode::PBR, light, nullptr, environment);
        auto fallbackPT       = render(PathIntegrator::PathTracing, 4),
             fallbackBDPT     = render(PathIntegrator::Bidirectional, 4);
        if (fallbackPT != fallbackBDPT)
            throw std::runtime_error("Environment/delta fallback changed pixels");
        settings.integrator = PathIntegrator::PathTracing;
        session.Render(model, nullptr, uniforms, settings, {0, 0, 0, 1}, size, size);
        if (session.Samples() != 4)
            throw std::runtime_error("Integrator change did not reset accumulation");
        std::cout << "PASS BDPT area-measure MIS, bounded lengths, energy, convergence and reset\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
