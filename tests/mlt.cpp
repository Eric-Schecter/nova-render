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
        if (argc != 4)
            throw std::runtime_error("Expected API, assets and output directory");
        RendererConfig    config;
        const std::string api = TestAPI(argv[1]);
        config.api = api == "metal" ? GraphicsAPI::Metal : api == "vulkan" ? GraphicsAPI::Vulkan : GraphicsAPI::D3D12;
        config.useWarp        = TestUseWarp(argv[1]);
        PathSession session(true);
        const auto  caps = session.Capabilities(config.api, config);
        if (!caps.pathTracing)
            return 77;
        if (!caps.metropolis)
        {
            RayTracingSettings settings;
            settings.integrator = RayIntegrator::Metropolis;
            bool rejected       = false;
            try
            {
                session.Render(nullptr, nullptr, {}, settings, {0, 0, 0, 1}, 16, 16);
            }
            catch (const std::runtime_error& error)
            {
                rejected = std::string(error.what()).find("MLT requires") != std::string::npos;
            }
            if (!rejected || std::string(argv[1]) == "dx12-hardware")
                throw std::runtime_error("MLT capability/rejection mismatch");
            std::cout << "SKIP MLT on DX12 WARP: unsupported dispatch is rejected before GPU submission" << std::endl;
            return 77;
        }
        const auto       model = LoadGLB(std::filesystem::path(argv[2]) / "CornellCaustics/CornellCaustics.glb");
        const uint32_t   size  = caps.softwareRayTracing ? 16 : 32;
        OrbitCamera      camera;
        DirectionalLight light;
        light.intensity = 0;
        EnvironmentSettings env;
        env.intensity            = 0;
        env.showBackground       = false;
        auto               scene = MakeSceneUniforms(*model, camera, 1, LightingMode::PBR, light, nullptr, env);
        RayTracingSettings settings;
        settings.samplesPerFrame     = 1;
        settings.resolutionScale     = 1;
        settings.maxBounces          = 8;
        settings.mltBootstrapSamples = 262144;
        auto run                     = [&](RayIntegrator method, uint32_t count) {
            settings.integrator = method;
            session.Reset();
            std::cout << "Running integrator " << int(method) << ", " << count << " steps, size " << size << std::endl;
            const ModelImage* image = nullptr;
            for (uint32_t i = 0; i < count; ++i)
            {
                image = &session.Render(model, nullptr, scene, settings, {0, 0, 0, 1}, size, size);
                if (i == 0)
                    std::cout << "First step completed" << std::endl;
            }
            std::vector<float> values(size * size);
            std::memcpy(values.data(), image->pixels.data(), image->pixels.size());
            for (float x : values)
                if (!std::isfinite(x) || x < 0)
                    throw std::runtime_error("Nonfinite MLT image");
            return values;
        };
        const auto reference = run(RayIntegrator::Bidirectional, caps.softwareRayTracing ? 512 : 8192);
        auto       metric    = [&](const std::vector<float>& values, bool error) {
            double sum = 0;
            size_t n   = 0;
            for (uint32_t y = size / 4; y < size * 9 / 10; ++y)
                for (uint32_t x = size / 5; x < size * 4 / 5; ++x)
                {
                    const auto i = y * size + x;
                    sum += error ? std::pow(values[i] - reference[i], 2) : values[i];
                    ++n;
                }
            return sum / n;
        };
        const auto early   = run(RayIntegrator::Metropolis, 32),
                   late    = run(RayIntegrator::Metropolis, caps.softwareRayTracing ? 512 : 2048);
        const auto refMean = metric(reference, false), mean = metric(late, false), mse0 = metric(early, true),
                   mse1 = metric(late, true);
        std::cout << "MLT mean=" << mean << " BDPT=" << refMean << " MSE32=" << mse0 << " later=" << mse1 << std::endl;
        if (std::abs(mean / refMean - 1) > (caps.softwareRayTracing ? .3 : .15) || mse1 >= mse0)
            throw std::runtime_error("MLT glass convergence/energy failed");
        std::filesystem::create_directories(argv[3]);
        std::ofstream file(std::filesystem::path(argv[3]) / "mlt-native.bin", std::ios::binary);
        file.write(reinterpret_cast<const char*>(late.data()), late.size() * 4);
        settings.mltSeed = 2;
        session.Render(model, nullptr, scene, settings, {0, 0, 0, 1}, size, size);
        if (session.Samples() != 1)
            throw std::runtime_error("MLT seed reset failed");
        settings.mltLargeStepProbability = .5f;
        session.Render(model, nullptr, scene, settings, {0, 0, 0, 1}, size, size);
        if (session.Samples() != 1)
            throw std::runtime_error("MLT probability reset failed");
        settings.mltMutationScale = 0;
        bool rejected             = false;
        try
        {
            ValidatePathTracing(settings);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Invalid MLT accepted");
        std::cout << "PASS MLT glass, energy, convergence, reset and validation\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
