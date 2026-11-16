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
            throw std::runtime_error("Expected API, assets and output");
        RendererConfig config;
        std::string    api = TestAPI(argv[1]);
        config.api = api == "metal" ? GraphicsAPI::Metal : api == "vulkan" ? GraphicsAPI::Vulkan : GraphicsAPI::D3D12;
        config.useWarp     = TestUseWarp(argv[1]);
        PathSession session(true);
        const auto  capabilities = session.Capabilities(config.api, config);
        if (!capabilities.pathTracing)
            return 77;
        auto   model = LoadGLB(std::filesystem::path(argv[2]) / "CornellCaustics/CornellCaustics.glb");
        size_t glass = 0;
        for (const auto& v : model->vertices)
            if (v.normal[3] == 1 && v.uv[3] == 1.5f)
                ++glass;
        if (glass < 1000)
            throw std::runtime_error("Missing dielectric material");
        OrbitCamera      camera;
        DirectionalLight light;
        light.intensity = 0;
        EnvironmentSettings env;
        env.intensity            = 0;
        env.showBackground       = false;
        auto               scene = MakeSceneUniforms(*model, camera, 1, LightingMode::PBR, light, nullptr, env);
        RayTracingSettings settings;
        settings.maxBounces      = 8;
        settings.samplesPerFrame = 4;
        settings.resolutionScale = 1;
        settings.photonRadius    = .1f;
        double reference         = 0;
        for (auto method : {RayIntegrator::PathTracing, RayIntegrator::Bidirectional, RayIntegrator::PhotonMapping})
        {
            settings.integrator = method;
            session.Reset();
            const ModelImage* image = nullptr;
            // Keep software correctness regression bounded. Metal retains the
            // 4096-spp run used for native/WebGPU pixel parity.
            const int target = capabilities.softwareRayTracing ? 512 : 4096;
            for (int i = 0; i < target; i += 4)
                image = &session.Render(model, nullptr, scene, settings, {0, 0, 0, 1}, 48, 48);
            double mean = 0;
            for (int y = 12; y < 43; ++y)
                for (int x = 9; x < 38; ++x)
                {
                    float f;
                    std::memcpy(&f, image->pixels.data() + (y * 48 + x) * 4, 4);
                    if (!std::isfinite(f) || f < 0)
                        throw std::runtime_error("Invalid glass radiance");
                    mean += f / 899;
                }
            std::cout << "Integrator " << int(method) << " spp=" << target << " mean=" << mean << std::endl;
            if (method == RayIntegrator::PathTracing)
                reference = mean;
            else if (std::abs(mean / reference - 1) > .2)
                throw std::runtime_error("Glass energy mismatch");
            if (method == RayIntegrator::PhotonMapping)
            {
                std::ofstream file(argv[3], std::ios::binary);
                file.write(reinterpret_cast<const char*>(image->pixels.data()), image->pixels.size());
            }
        }
        std::cout << "PASS closed glass, PT/BDPT/photon energy and native parity output" << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
