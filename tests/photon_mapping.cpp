#include "TestDevice.hpp"
#include "path/PathTracing.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace render;
using namespace render::detail;
int main(int argc, char** argv)
{
    try
    {
        if (argc != 3)
            throw std::runtime_error("Expected API and assets root");
        RendererConfig    config;
        const std::string api = TestAPI(argv[1]);
        config.api = api == "metal" ? GraphicsAPI::Metal : api == "vulkan" ? GraphicsAPI::Vulkan : GraphicsAPI::D3D12;
        config.useWarp        = TestUseWarp(argv[1]);
        PathSession session(true);
        const auto  caps = session.Capabilities(config.api, config);
        if (!caps.pathTracing)
        {
            std::cout << "SKIP " << caps.reason << '\n';
            return 77;
        }
        auto        model = LoadGLB(std::filesystem::path(argv[2]) / "CornellBox/CornellBox.glb");
        OrbitCamera camera;
        camera.yaw = camera.pitch = 0;
        camera.distance           = 3.6;
        DirectionalLight light;
        light.intensity = 0;
        EnvironmentSettings env;
        env.intensity                = 0;
        env.showBackground           = false;
        auto                uniforms = MakeSceneUniforms(*model, camera, 1, LightingMode::PBR, light, nullptr, env);
        PathTracingSettings settings;
        settings.samplesPerFrame = 4;
        settings.resolutionScale = 1;
        settings.maxBounces      = 5;
        const uint32_t size      = caps.softwareRayTracing ? 24 : 48;
        auto           render    = [&](PathIntegrator integrator, int spp) {
            session.Reset();
            settings.integrator = integrator;
            std::vector<float> values;
            for (int i = 0; i < spp; i += 4)
            {
                const auto& image = session.Render(model, nullptr, uniforms, settings, {0, 0, 0, 1}, size, size);
                if (i + 4 == spp)
                {
                    values.resize(size * size);
                    std::memcpy(values.data(), image.pixels.data(), image.pixels.size());
                }
            }
            for (float x : values)
                if (!std::isfinite(x) || x < 0)
                    throw std::runtime_error("Invalid photon radiance");
            return values;
        };
        const auto reference = render(PathIntegrator::PathTracing, 4096),
                   early = render(PathIntegrator::PhotonMapping, 8), later = render(PathIntegrator::PhotonMapping, 512);
        auto metric = [&](const auto& values, bool error) {
            double sum   = 0;
            size_t count = 0;
            for (uint32_t y = size / 4; y < size * 9 / 10; y++)
                for (uint32_t x = size / 5; x < size * 4 / 5; x++)
                {
                    const auto i = y * size + x;
                    sum += error ? std::pow(values[i] - reference[i], 2) : values[i];
                    count++;
                }
            return sum / count;
        };
        const double energy = std::abs(metric(later, false) / metric(reference, false) - 1);
        std::cout << "Photon MSE 8=" << metric(early, true) << " 512=" << metric(later, true)
                  << " mean=" << metric(later, false) << " PT mean=" << metric(reference, false)
                  << " energy difference=" << energy << std::endl;
        if (!(metric(later, true) < metric(early, true)) || energy > .1)
            throw std::runtime_error("Photon convergence/energy failed");
        if (api == "metal")
        {
            std::ofstream  out("photon-native-raw.bin", std::ios::binary);
            const uint32_t shape[2] = {size, size};
            out.write(reinterpret_cast<const char*>(shape), sizeof(shape));
            out.write(reinterpret_cast<const char*>(later.data()), std::streamsize(later.size() * sizeof(float)));
        }
        for (const auto* name : {"CornellIndirect", "CornellGlossy"})
        {
            model                   = LoadGLB(std::filesystem::path(argv[2]) / name / (std::string(name) + ".glb"));
            uniforms                = MakeSceneUniforms(*model, camera, 1, LightingMode::PBR, light, nullptr, env);
            const auto ref          = render(PathIntegrator::Bidirectional, 2048),
                       estimate     = render(PathIntegrator::PhotonMapping, 256);
            const double difference = std::abs(metric(estimate, false) / metric(ref, false) - 1);
            std::cout << name << " photon/BDPT energy difference=" << difference << std::endl;
            if (difference > .2)
                throw std::runtime_error("Variant photon energy failed");
        }
        settings.photonRadius = .12f;
        session.Render(model, nullptr, uniforms, settings, {0, 0, 0, 1}, size, size);
        if (session.Samples() != 4)
            throw std::runtime_error("Photon radius did not reset");
        settings.photonsPerIteration = 512;
        settings.denoise             = true;
        session.Render(model, nullptr, uniforms, settings, {0, 0, 0, 1}, size, size);
        if (session.Samples() != 4)
            throw std::runtime_error("Photon budget did not reset");
        settings.integrator = PathIntegrator::Bidirectional;
        session.Render(model, nullptr, uniforms, settings, {0, 0, 0, 1}, size, size);
        PathSession color;
        if (!color.Capabilities(config.api, config).pathTracing)
            throw std::runtime_error("Color device unavailable");
        settings.integrator = PathIntegrator::PhotonMapping;
        settings.denoise    = false;
        const auto raw      = color.Render(model, nullptr, uniforms, settings, {0, 0, 0, 1}, 32, 32).pixels;
        settings.denoise    = true;
        const auto filtered = color.Render(model, nullptr, uniforms, settings, {0, 0, 0, 1}, 32, 32).pixels;
        if (raw == filtered || color.Samples() != 4)
            throw std::runtime_error("Photon denoise did not execute/reset");
        std::cout << "PASS emitted photon normalization, density convergence, scratch resizing, denoise and integrator "
                     "switch\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
