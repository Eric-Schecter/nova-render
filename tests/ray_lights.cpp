#include "TestDevice.hpp"
#include "path/PathTracing.hpp"
#include <cmath>
#include <iostream>
using namespace render;
void require(bool ok, const char* text)
{
    if (!ok)
        throw std::runtime_error(text);
}
double energy(const Image& image)
{
    double total = 0;
    for (size_t i = 0; i < image.pixels.size(); i += 4)
    {
        float v;
        std::memcpy(&v, image.pixels.data() + i, 4);
        require(std::isfinite(v) && v >= 0, "Nonfinite ray radiance");
        total += v;
    }
    return total / (image.width * image.height);
}
int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "Expected API");
        const std::string api = TestAPI(argv[1]);
        RendererConfig    config;
        config.api = api == "metal" ? GraphicsAPI::Metal : api == "vulkan" ? GraphicsAPI::Vulkan : GraphicsAPI::D3D12;
        config.useWarp = TestUseWarp(argv[1]);
        detail::PathSession session(true);
        auto                caps = session.Capabilities(config.api, config);
        require(caps.pathTracing, "Ray device unavailable");
        auto model      = std::make_shared<detail::ModelData>(*detail::EmptyModelData());
        model->vertices = {
          {{-1, -1, 0, 1}, {0, 0, 1, 0}, {0, 0, 0, 0}},
          {{1, -1, 0, 1}, {0, 0, 1, 0}, {1, 0, 0, 0}},
          {{1, 1, 0, 1}, {0, 0, 1, 0}, {1, 1, 0, 0}},
          {{-1, -1, 0, 1}, {0, 0, 1, 0}, {0, 0, 0, 0}},
          {{1, 1, 0, 1}, {0, 0, 1, 0}, {1, 1, 0, 0}},
          {{-1, 1, 0, 1}, {0, 0, 1, 0}, {0, 1, 0, 0}}};
        model->images[0].pixels = {200, 200, 200, 255};
        model->material         = {0, .8f, 1, 1};
        EnvironmentSettings environment;
        environment.intensity      = 0;
        environment.showBackground = false;
        DirectionalLight legacy;
        legacy.intensity = 100;
        OrbitCamera camera;
        camera.distance = 3.6f;
        const auto uniforms =
          detail::MakeSceneUniforms(*model, camera, 1, LightingMode::PBR, legacy, nullptr, environment);
        PathTracingSettings settings;
        settings.resolutionScale = 1;
        settings.maxBounces      = 1;
        settings.samplesPerFrame = 1;
        SceneShadowSettings shadows;
        shadows.enabled = true;
        auto render     = [&](const std::vector<SceneLight>& lights) {
            session.Reset();
            return session.Render(model, nullptr, uniforms, settings, {0, 0, 0, 1}, 48, 48, nullptr, &lights, shadows);
        };
        std::vector<SceneLight> lights(6);
        for (size_t i = 0; i < 6; i++)
        {
            auto& l     = lights[i];
            l.id        = std::to_string(i);
            l.intensity = i < 2 ? 2 : 8;
            l.color     = {i % 3 == 0 ? 1.f : 0.f, i % 3 == 1 ? 1.f : 0.f, i % 3 == 2 ? 1.f : 0.f};
            l.type      = i < 2 ? LightType::Directional : i < 4 ? LightType::Point : LightType::Spot;
            l.position  = {i % 2 ? 1.f : -1.f, 0, 2};
            l.direction =
              i < 2 ? std::array<float, 3>{i % 2 ? 1.f : -1.f, 0, 2} : std::array<float, 3>{i % 2 ? -.5f : .5f, 0, -1};
            l.outerConeAngle = 1;
            l.shadowSoftness = 0;
        }
        auto empty = render({});
        require(energy(empty) < 1e-8, "Empty collection retained legacy light");
        double singles = 0;
        for (const auto& l : lights)
        {
            auto e = energy(render({l}));
            require(e > .001, "Single ray light contributed nothing");
            singles += e;
        }
        const auto sum = energy(render(lights));
        require(std::abs(sum - singles) < std::max(1e-6, singles * 1e-4), "Ray lights not additive");
        for (size_t i = 0; i < lights.size(); i++)
        {
            auto subset = lights;
            subset.erase(subset.begin() + i);
            require(energy(render(subset)) < sum - .001, "Light absent from ray sum");
        }
        for (auto& l : lights)
            l.enabled = false;
        require(energy(render(lights)) < 1e-8, "Disabled ray lights contribute");
        for (auto& l : lights)
            l.enabled = true;
        auto moved        = lights[2];
        moved.position[2] = 20;
        require(energy(render({moved})) < energy(render({lights[2]})) * .1, "Point distance attenuation missing");
        moved.range = .1f;
        require(energy(render({moved})) < 1e-8, "Point range ignored");
        moved           = lights[4];
        moved.direction = {0, 0, 1};
        require(energy(render({moved})) < 1e-8, "Spot direction ignored");
        session.Reset();
        session.Render(model, nullptr, uniforms, settings, {0, 0, 0, 1}, 48, 48, nullptr, &lights, shadows);
        session.Render(model, nullptr, uniforms, settings, {0, 0, 0, 1}, 48, 48, nullptr, &lights, shadows);
        require(session.Samples() == 2, "Ray accumulation stopped");
        lights[0].color[0] = .5f;
        session.Render(model, nullptr, uniforms, settings, {0, 0, 0, 1}, 48, 48, nullptr, &lights, shadows);
        require(session.Samples() == 1, "Light edit did not reset accumulation");
        // Offset blocker intersects the source segment but leaves the receiver visible to camera.
        auto blocked = std::make_shared<detail::ModelData>(*model);
        blocked->vertices.insert(
          blocked->vertices.end(),
          {{{-.7f, -.4f, .5f, 1}, {0, 0, 1, 0}, {0, 0, 0, 0}},
           {{-.3f, -.4f, .5f, 1}, {0, 0, 1, 0}, {1, 0, 0, 0}},
           {{-.3f, .4f, .5f, 1}, {0, 0, 1, 0}, {1, 1, 0, 0}},
           {{-.7f, -.4f, .5f, 1}, {0, 0, 1, 0}, {0, 0, 0, 0}},
           {{-.3f, .4f, .5f, 1}, {0, 0, 1, 0}, {1, 1, 0, 0}},
           {{-.7f, .4f, .5f, 1}, {0, 0, 1, 0}, {0, 1, 0, 0}}});
        model = blocked;
        SceneLight local;
        local.id             = "shadow";
        local.type           = LightType::Point;
        local.position       = {-1, 0, 1};
        local.intensity      = 8;
        local.shadowSoftness = 0;
        for (auto type : {LightType::Point, LightType::Spot})
        {
            local.type           = type;
            local.direction      = {1, 0, -1};
            local.outerConeAngle = 1;
            local.castShadow     = false;
            double off           = energy(render({local}));
            local.castShadow     = true;
            double on            = energy(render({local}));
            require(on < off * .99, "Ray local shadow missing");
            shadows.enabled = false;
            require(std::abs(energy(render({local})) - off) < 1e-6, "Ray shadow master ignored");
            shadows.enabled = true;
        }
        // Each integrator retains its camera-path NEE contribution for analytic sources.
        for (auto integrator :
             {PathIntegrator::PathTracing,
              PathIntegrator::Bidirectional,
              PathIntegrator::PhotonMapping,
              PathIntegrator::Metropolis})
        {
            if (integrator == PathIntegrator::Metropolis && !caps.metropolis)
                continue;
            settings.integrator          = integrator;
            settings.mltBootstrapSamples = 256;
            settings.photonsPerIteration = 256;
            local.castShadow             = false;
            require(energy(render({local})) > .001, "Integrator lost analytic light");
        }
        std::cout << "PASS " << api
                  << " ray lights: six-light additivity, range/cone, shadows, accumulation and four integrators\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
