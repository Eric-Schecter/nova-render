#include "TestDevice.hpp"
#include "path/PathTracing.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
using namespace render;
using namespace render::detail;
double energy(const Image& image, double& peak)
{
    double sum = 0;
    for (size_t i = 0; i < image.pixels.size(); i += 4)
    {
        float value;
        std::memcpy(&value, image.pixels.data() + i, 4);
        if (!std::isfinite(value) || value < 0)
            throw std::runtime_error("Invalid dielectric radiance");
        sum += value;
        peak = std::max(peak, double(value));
    }
    return sum / (image.width * image.height);
}
int main(int argc, char** argv)
{
    try
    {
        if (argc != 2)
            throw std::runtime_error("Expected API");
        RendererConfig config;
        config.api = TestAPI(argv[1]) == "metal" ? GraphicsAPI::Metal : GraphicsAPI::Vulkan;
        PathSession session(true);
        if (!session.Capabilities(config.api, config).pathTracing)
            throw std::runtime_error("No ray device");
        auto model      = std::make_shared<ModelData>(*EmptyModelData());
        model->vertices = {
          {{-1, -1, 0, 1}, {0, 0, 1, 1}, {0, 0, 0, 1.5f}},
          {{1, -1, 0, 1}, {0, 0, 1, 1}, {1, 0, 0, 1.5f}},
          {{1, 1, 0, 1}, {0, 0, 1, 1}, {1, 1, 0, 1.5f}},
          {{-1, -1, 0, 1}, {0, 0, 1, 1}, {0, 0, 0, 1.5f}},
          {{1, 1, 0, 1}, {0, 0, 1, 1}, {1, 1, 0, 1.5f}},
          {{-1, 1, 0, 1}, {0, 0, 1, 1}, {0, 1, 0, 1.5f}}};
        model->material = {0, .4f, 1, 1};
        OrbitCamera camera;
        camera.yaw = camera.pitch = 0;
        camera.distance           = 3.6;
        EnvironmentSettings env;
        env.intensity      = 0;
        env.showBackground = false;
        DirectionalLight legacy;
        legacy.direction             = {0, 0, -1};
        legacy.intensity             = .06f;
        auto                uniforms = MakeSceneUniforms(*model, camera, 1, LightingMode::PBR, legacy, nullptr, env);
        PathTracingSettings settings;
        settings.samplesPerFrame     = 4;
        settings.resolutionScale     = 1;
        settings.maxBounces          = 1;
        settings.photonsPerIteration = 256;
        SceneShadowSettings shadows;
        shadows.enabled = true;
        std::vector<SceneLight> lights(1);
        auto&                   light = lights[0];
        light.id                      = "behind-interface";
        light.position                = {0, 0, -2};
        light.direction               = {0, 0, 1};
        light.intensity               = .06f;
        light.castShadow              = true;
        light.shadowSoftness          = 0;
        light.innerConeAngle          = .6f;
        light.outerConeAngle          = 1;
        double peak                   = 0;
        auto   render                 = [&](PathIntegrator integrator, const std::vector<SceneLight>* collection) {
            settings.integrator = integrator;
            peak                = 0;
            session.Reset();
            double e = 0;
            for (int i = 0; i < 64; i += 4)
                e = energy(
                  session.Render(
                    model, nullptr, uniforms, settings, {0, 0, 0, 1}, 32, 32, nullptr, collection, shadows),
                  peak);
            return e;
        };
        for (auto type : {LightType::Point, LightType::Spot})
        {
            light.type       = type;
            shadows.enabled  = false;
            double reference = render(PathIntegrator::PathTracing, &lights);
            if (reference < 1e-5)
                throw std::runtime_error("Glass direct transmission was discarded");
            shadows.enabled = true;
            for (auto integrator :
                 {PathIntegrator::PathTracing, PathIntegrator::Bidirectional, PathIntegrator::PhotonMapping})
            {
                const auto value = render(integrator, &lights);
                std::cout << "type=" << int(type) << " integrator=" << int(integrator) << " shadowed=" << value
                          << " reference=" << reference << std::endl;
                if (std::abs(value / reference - 1) > .001)
                    throw std::runtime_error("Interface self-shadow or direct energy mismatch");
                light.intensity   = 100;
                const auto bright = render(integrator, &lights);
                if (peak <= 64)
                    throw std::runtime_error("Bright fixture did not exercise the old clamp");
                if (std::abs(bright / (reference * (100 / .06)) - 1) > .001)
                    throw std::runtime_error("Hidden radiance clamp broke intensity linearity");
                light.intensity = .06f;
            }
        }
        if (render(PathIntegrator::PathTracing, nullptr) < 1e-5)
            throw std::runtime_error("Legacy directional transmission discarded");
        auto blocked = std::make_shared<ModelData>(*model);
        for (auto vertex : model->vertices)
        {
            vertex.position[2] = -1;
            vertex.normal[3]   = 0;
            blocked->vertices.push_back(vertex);
        }
        model = blocked;
        for (auto integrator :
             {PathIntegrator::PathTracing, PathIntegrator::Bidirectional, PathIntegrator::PhotonMapping})
            if (render(integrator, &lights) > 1e-8)
                throw std::runtime_error("Real blocker was ignored");
        std::cout << "PASS glass hemisphere, signed visibility bias, real blocker and three-integrator direct energy\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
