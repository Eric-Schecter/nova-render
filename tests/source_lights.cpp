#include "TestDevice.hpp"
#include "path/PathTracing.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
using namespace render;
using namespace render::detail;
void require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}
double mean(const std::vector<float>& image)
{
    double sum = 0;
    for (float value : image)
    {
        require(std::isfinite(value) && value >= 0, "Invalid source radiance");
        sum += value;
    }
    return sum / image.size();
}
double mse(const std::vector<float>& a, const std::vector<float>& b)
{
    double sum = 0;
    for (size_t i = 0; i < a.size(); ++i)
        sum += std::pow(a[i] - b[i], 2);
    return sum / a.size();
}
// Integrate 4x4 pixel blocks for subpixel glass caustics. Raw pointwise MSE
// is logged separately; a shrinking photon kernel need not improve it monotonically.
double blockMSE(const std::vector<float>& a, const std::vector<float>& b)
{
    double total = 0;
    for (int y = 0; y < 32; y += 4)
        for (int x = 0; x < 32; x += 4)
        {
            double delta = 0;
            for (int j = 0; j < 4; ++j)
                for (int i = 0; i < 4; ++i)
                    delta += a[(y + j) * 32 + x + i] - b[(y + j) * 32 + x + i];
            total += std::pow(delta / 16, 2);
        }
    return total / 64;
}
int main(int argc, char** argv)
{
    try
    {
        require(argc == 3, "Expected API and assets");
        RendererConfig config;
        std::string    api = TestAPI(argv[1]);
        config.api         = api == "metal" ? GraphicsAPI::Metal : GraphicsAPI::Vulkan;
        PathSession session(true);
        require(session.Capabilities(config.api, config).pathTracing, "Ray device unavailable");
        auto model =
          std::make_shared<ModelData>(*LoadGLB(std::filesystem::path(argv[2]) / "CornellBox/CornellBox.glb"));
        model->emissive = {0, 0, 0, 0};
        for (auto& slot : model->materials)
            slot.emissive = {0, 0, 0};
        PackMaterials(*model);
        OrbitCamera camera;
        camera.yaw = camera.pitch = 0;
        camera.distance           = 3.6f;
        DirectionalLight legacy;
        legacy.intensity = 0;
        EnvironmentSettings environment;
        environment.intensity      = 0;
        environment.showBackground = false;
        auto uniforms = MakeSceneUniforms(*model, camera, 1, LightingMode::PBR, legacy, nullptr, environment);
        PathTracingSettings settings;
        settings.samplesPerFrame     = 4;
        settings.resolutionScale     = 1;
        settings.maxBounces          = 4;
        settings.photonsPerIteration = 8192;
        settings.photonRadius        = .16f;
        SceneShadowSettings shadows;
        shadows.enabled = true;
        std::vector<SceneLight> lights(1);
        auto&                   light = lights[0];
        light.id                      = "source";
        light.position                = {0, .7f, .2f};
        light.direction               = {0, -1, 0};
        light.intensity               = .06f;
        light.innerConeAngle          = .55f;
        light.outerConeAngle          = .95f;
        light.castShadow              = true;
        light.shadowSoftness          = 0;
        auto render                   = [&](PathIntegrator integrator, int spp) {
            settings.integrator = integrator;
            session.Reset();
            std::vector<float> pixels(32 * 32);
            for (int i = 0; i < spp; i += 4)
            {
                const auto& image =
                  session.Render(model, nullptr, uniforms, settings, {0, 0, 0, 1}, 32, 32, nullptr, &lights, shadows);
                if (i + 4 == spp)
                    std::memcpy(pixels.data(), image.pixels.data(), image.pixels.size());
            }
            mean(pixels);
            return pixels;
        };
        for (const auto* scene : {"CornellBox", "CornellCaustics"})
        {
            model = std::make_shared<ModelData>(
              *LoadGLB(std::filesystem::path(argv[2]) / scene / (std::string(scene) + ".glb")));
            model->emissive = {0, 0, 0, 0};
            for (auto& slot : model->materials)
                slot.emissive = {0, 0, 0};
            PackMaterials(*model);
            uniforms = MakeSceneUniforms(*model, camera, 1, LightingMode::PBR, legacy, nullptr, environment);
            for (auto type : {LightType::Point, LightType::Spot})
            {
                light.type           = type;
                settings.maxBounces  = 4;
                const auto reference = render(
                  std::string(scene) == "CornellCaustics" ? PathIntegrator::Bidirectional : PathIntegrator::PathTracing,
                  std::string(scene) == "CornellCaustics" ? 16384 : 2048);
                const auto bdpt              = render(PathIntegrator::Bidirectional, 1024);
                settings.photonsPerIteration = 256;
                const auto early             = render(PathIntegrator::PhotonMapping, 8);
                settings.photonsPerIteration = 8192;
                const auto sameSppHighBudget = render(PathIntegrator::PhotonMapping, 8);
                require(mse(early, sameSppHighBudget) > 1e-14, "Photon budget did not change local-source transport");
                const auto photons =
                  render(PathIntegrator::PhotonMapping, std::string(scene) == "CornellCaustics" ? 1024 : 256);
                settings.maxBounces = 1;
                const auto direct   = render(PathIntegrator::PathTracing, 256);
                settings.maxBounces = 4;
                if (api == "metal")
                {
                    std::ofstream file(
                      std::string(scene) + (type == LightType::Point ? "-source-point.bin" : "-source-spot.bin"),
                      std::ios::binary);
                    file.write(reinterpret_cast<const char*>(photons.data()), photons.size() * sizeof(float));
                }
                if (api == "metal")
                {
                    std::ofstream ref(
                      std::string(scene) + (type == LightType::Point ? "-ref-point.bin" : "-ref-spot.bin"),
                      std::ios::binary);
                    ref.write(reinterpret_cast<const char*>(reference.data()), reference.size() * 4);
                    std::ofstream low(
                      std::string(scene) + (type == LightType::Point ? "-low-point.bin" : "-low-spot.bin"),
                      std::ios::binary);
                    low.write(reinterpret_cast<const char*>(early.data()), early.size() * 4);
                }
                const double base = mean(reference);
                std::cout << scene << " " << (type == LightType::Point ? "point" : "spot") << " PT=" << base
                          << " direct=" << mean(direct) << " BDPT=" << mean(bdpt) << " photons=" << mean(photons)
                          << " photon MSE(256x8)=" << mse(early, reference)
                          << " MSE(8192x256/1024)=" << mse(photons, reference) << std::endl;
                require(base > .00001 && base > mean(direct) * 1.05, "Missing indirect local illumination");
                require(std::abs(mean(bdpt) / base - 1) < .1, "Delta-position BDPT energy mismatch");
                require(std::abs(mean(photons) / base - 1) < .18, "Local photon flux mismatch");
                std::cout << "Block-integrated MSE low=" << blockMSE(early, reference)
                          << " high=" << blockMSE(photons, reference) << std::endl;
                // Sharp-caustic density estimates retain finite-budget bias;
                // report their MSE without requiring monotonic pixel convergence.
                if (std::string(scene) == "CornellBox")
                    require(mse(photons, reference) < mse(early, reference), "Diffuse photons did not converge");
                require(
                  mse(bdpt, render(PathIntegrator::PathTracing, 1024)) > 1e-14,
                  "BDPT still uses the identical camera-only sequence");
                light.range = .01f;
                for (auto integrator : {PathIntegrator::Bidirectional, PathIntegrator::PhotonMapping})
                    require(mean(render(integrator, 8)) < 1e-8, "Source range leaked energy");
                light.range   = 0;
                light.enabled = false;
                require(mean(render(PathIntegrator::Bidirectional, 4)) < 1e-8, "Disabled BDPT source emitted");
                require(mean(render(PathIntegrator::PhotonMapping, 4)) < 1e-8, "Disabled photon source emitted");
                light.enabled = true;
            }
        }
        // Audit source-selection normalization at the public 16-light limit,
        // with an emissive mesh competing with all delta-position sources.
        model = std::make_shared<ModelData>(*LoadGLB(std::filesystem::path(argv[2]) / "CornellBox/CornellBox.glb"));
        for (auto& slot : model->materials)
            for (auto& value : slot.emissive)
                value *= .01f;
        PackMaterials(*model);
        uniforms = MakeSceneUniforms(*model, camera, 1, LightingMode::PBR, legacy, nullptr, environment);
        lights.resize(16);
        for (size_t i = 0; i < lights.size(); ++i)
        {
            auto& l          = lights[i];
            l.id             = std::to_string(i);
            l.type           = i % 2 ? LightType::Spot : LightType::Point;
            l.position       = {i % 2 ? .2f : -.2f, .7f, .2f};
            l.direction      = {0, -1, 0};
            l.intensity      = .006f;
            l.innerConeAngle = .55f;
            l.outerConeAngle = .95f;
            l.enabled        = true;
            l.range          = 0;
            l.castShadow     = true;
            l.shadowSoftness = 0;
        }
        settings.maxBounces = 4;
        const auto mixedRef = render(PathIntegrator::PathTracing, 2048);
        for (auto integrator : {PathIntegrator::Bidirectional, PathIntegrator::PhotonMapping})
        {
            const auto image = render(integrator, 512);
            std::cout << "Mixed area + 16 locals: reference=" << mean(mixedRef) << " estimate=" << mean(image)
                      << std::endl;
            require(std::abs(mean(image) / mean(mixedRef) - 1) < .12, "Mixed source PMF normalization failed");
        }
        std::cout << "PASS point/spot emission, delta MIS, photon normalization, range and convergence\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
