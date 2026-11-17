#include "TestDevice.hpp"
#include "path/PathTracing.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <numeric>
using namespace render;
using namespace render::detail;
struct Estimate
{
    double mean = 0, se = 0;
};
int main(int argc, char** argv)
{
    try
    {
        if (argc != 3)
            throw std::runtime_error("Expected API and assets");
        RendererConfig config;
        config.api = TestAPI(argv[1]) == "metal" ? GraphicsAPI::Metal : GraphicsAPI::Vulkan;
        if (const char* path = std::getenv("NOVA_CAUSTIC_SHADERS"))
            config.shaderDirectory = path;
        PathSession session(true);
        if (!session.Capabilities(config.api, config).pathTracing)
            throw std::runtime_error("Ray tracing unavailable");
        auto model =
          std::make_shared<ModelData>(*LoadGLB(std::filesystem::path(argv[2]) / "CornellCaustics/CornellCaustics.glb"));
        model->emissive = {0, 0, 0, 0};
        for (auto& m : model->materials)
            m.emissive = {0, 0, 0};
        PackMaterials(*model);
        OrbitCamera camera;
        camera.yaw = camera.pitch = 0;
        camera.distance           = 3.6;
        DirectionalLight legacy;
        legacy.intensity = 0;
        EnvironmentSettings env;
        env.intensity      = 0;
        env.showBackground = false;
        auto uniforms      = MakeSceneUniforms(*model, camera, 1, LightingMode::PBR, legacy, nullptr, env);
        std::vector<SceneLight> lights(1);
        auto&                   lamp = lights[0];
        lamp.id                      = "spot";
        lamp.type                    = LightType::Spot;
        lamp.position                = {0, .7f, .2f};
        lamp.direction               = {0, -1, 0};
        lamp.intensity               = .06f;
        lamp.innerConeAngle          = .55f;
        lamp.outerConeAngle          = .95f;
        lamp.castShadow              = true;
        lamp.shadowSoftness          = 0;
        SceneShadowSettings shadows;
        shadows.enabled = true;
        PathTracingSettings settings;
        settings.maxBounces      = 4;
        settings.samplesPerFrame = 4;
        settings.resolutionScale = 1;
        const int spp            = std::getenv("NOVA_AUDIT_SPP") ? std::atoi(std::getenv("NOVA_AUDIT_SPP")) : 32768;
        if (spp < 1024 || spp % 256)
            throw std::runtime_error("Audit SPP must be a multiple of 256, at least 1024");
        auto render = [&](const char* name, PathIntegrator integrator, int samples) {
            settings.integrator = integrator;
            session.Reset();
            double              previous = 0;
            std::vector<double> blocks;
            std::vector<float>  pixels(32 * 32);
            for (int i = 0; i < samples; i += 4)
            {
                const auto& image =
                  session.Render(model, nullptr, uniforms, settings, {0, 0, 0, 1}, 32, 32, nullptr, &lights, shadows);
                if ((i + 4) % 256 == 0)
                {
                    std::memcpy(pixels.data(), image.pixels.data(), image.pixels.size());
                    double mean = std::accumulate(pixels.begin(), pixels.end(), 0.0) / pixels.size();
                    for (float v : pixels)
                        if (!std::isfinite(v) || v < 0)
                            throw std::runtime_error("Invalid caustic radiance");
                    blocks.push_back(((i + 4) * mean - (i + 4 - 256) * previous) / 256);
                    previous = mean;
                }
            }
            Estimate result;
            result.mean     = previous;
            double variance = 0;
            for (double v : blocks)
                variance += (v - result.mean) * (v - result.mean);
            result.se = std::sqrt(variance / (blocks.size() * (blocks.size() - 1)));
            std::cout << "{\"case\":\"" << name << "\",\"intensity\":" << lamp.intensity << ",\"spp\":" << samples
                      << ",\"radius\":" << settings.photonRadius << ",\"photons\":" << settings.photonsPerIteration
                      << ",\"mean\":" << result.mean << ",\"se\":" << result.se << "}" << std::endl;
            return result;
        };
        const auto reference  = render("BDPT", PathIntegrator::Bidirectional, spp);
        auto       compatible = [&](Estimate result) {
            const double tolerance = .03 * reference.mean + 4 * std::hypot(result.se, reference.se);
            if (std::abs(result.mean - reference.mean) > tolerance)
                throw std::runtime_error("Caustic energy exceeds 3% plus four block standard errors");
        };
        compatible(render("PT", PathIntegrator::PathTracing, spp));
        for (float radius : {.03f, .16f, .32f})
        {
            settings.photonRadius = radius;
            compatible(render("photon-radius", PathIntegrator::PhotonMapping, 4096));
        }
        std::cout << "PASS independent PT/BDPT confidence bounds and photon radius sweep\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
