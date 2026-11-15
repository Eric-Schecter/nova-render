#include "ModelData.hpp"
#include "path/PathTracing.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <render/RayTracingView.hpp>
#include <set>
using namespace render;
static std::shared_ptr<detail::ModelData> Plane()
{
    auto m = std::make_shared<detail::ModelData>();
    for (auto& image : m->images)
    {
        image.width = image.height = 1;
        image.pixels               = {255, 255, 255, 255};
    }
    m->images[4].pixels = {0, 0, 0, 255};
    for (const auto& p :
         std::array<std::array<float, 3>, 6>{{{-2, -2, 0}, {2, -2, 0}, {2, 2, 0}, {-2, -2, 0}, {2, 2, 0}, {-2, 2, 0}}})
    {
        detail::ModelVertex v{};
        std::copy(p.begin(), p.end(), v.position);
        v.position[3] = 1;
        v.normal[2]   = 1;
        m->vertices.push_back(v);
    }
    return m;
}
int main(int argc, char** argv)
{
    try
    {
        if (argc < 3)
            return 1;
        RendererConfig    config;
        const std::string api = argv[1];
        config.api = api == "metal" ? GraphicsAPI::Metal : api == "vulkan" ? GraphicsAPI::Vulkan : GraphicsAPI::D3D12;
        config.useWarp = api == "dx12";
        detail::PathSession session(true);
        if (!session.Capabilities(config.api, config).pathTracing)
            return 77;
        AmbientOcclusionSettings ao;
        ao.enabled         = true;
        ao.output          = AOOutput::Occlusion;
        ao.denoise         = false;
        ao.resolutionScale = 1;
        ao.radius          = .4f;
        ao.samples         = 32;
        RayTracingSettings settings;
        OrbitCamera        camera;
        DirectionalLight   light;
        light.intensity = 0;
        auto plane      = Plane();
        for (int method = 0; method < 5; method++)
        {
            ao.method     = static_cast<AOMethod>(method);
            auto  uniform = detail::MakeSceneUniforms(*plane, camera, 1, LightingMode::PBR, light);
            auto& image   = session.Render(plane, nullptr, uniform, settings, {0, 0, 0, 1}, 32, 32, &ao);
            for (size_t i = 0; i < image.pixels.size(); i += 4)
            {
                float v;
                std::memcpy(&v, image.pixels.data() + i, 4);
                if (!std::isfinite(v) || std::abs(v - 1) > 1e-6)
                    throw std::runtime_error("Unoccluded plane is not white");
            }
        }
        const auto    model   = detail::LoadGLB(std::filesystem::path(argv[2]) / "CornellBox/CornellBox.glb");
        const auto    uniform = detail::MakeSceneUniforms(*model, camera, 1, LightingMode::PBR, light);
        std::ofstream json;
        if (argc > 3)
        {
            const auto parent = std::filesystem::path(argv[3]).parent_path();
            if (!parent.empty())
                std::filesystem::create_directories(parent);
            json.open(argv[3]);
            json << "{\"width\":32,\"height\":32,\"methods\":[";
        }
        std::set<std::vector<unsigned char>> results;
        for (int method = 1; method < 5; method++)
        {
            ao.method      = static_cast<AOMethod>(method);
            auto&  image   = session.Render(model, nullptr, uniform, settings, {0, 0, 0, 1}, 32, 32, &ao);
            double sum     = 0;
            float  minimum = 1;
            if (json.is_open())
            {
                if (method > 1)
                    json << ',';
                json << "[";
            }
            for (size_t i = 0; i < image.pixels.size(); i += 4)
            {
                float v;
                std::memcpy(&v, image.pixels.data() + i, 4);
                if (!std::isfinite(v) || v < 0 || v > 1)
                    throw std::runtime_error("AO outside visibility range");
                sum += v;
                minimum = std::min(minimum, v);
                if (json.is_open())
                {
                    if (i)
                        json << ',';
                    json << v;
                }
            }
            if (json.is_open())
                json << "]";
            sum /= 1024;
            results.insert(image.pixels);
            std::cout << method << " mean=" << sum << " min=" << minimum << '\n';
            if (sum >= .999 || sum < .05)
                throw std::runtime_error("AO failed to detect corner occlusion");
        }
        if (json.is_open())
            json << "]}\n";
        if (results.size() != 4)
            throw std::runtime_error("AO variants produce identical buffers");
        auto changed   = camera;
        changed.target = {.1f, .1f, 0};
        auto next      = detail::MakeSceneUniforms(*model, changed, 1, LightingMode::PBR, light);
        session.Render(model, nullptr, next, settings, {0, 0, 0, 1}, 32, 32, &ao);
        if (session.Samples() != 1)
            throw std::runtime_error("Target change did not reset accumulation");
        auto bad      = ao;
        bad.radius    = 0;
        bool rejected = false;
        try
        {
            detail::ValidateAmbientOcclusion(bad);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Bad AO settings accepted");
        const auto sponza = detail::LoadGLB(std::filesystem::path(argv[2]) / "SponzaWhite/SponzaWhite.glb");
        if (sponza->vertices.size() / 3 < 100000)
            throw std::runtime_error("Sponza geometry missing");
        render::RayTracingView visual(config);
        visual.LoadModel(std::filesystem::path(argv[2]) / "SponzaWhite/SponzaWhite.glb");
        OrbitCamera interior;
        interior.yaw      = 90;
        interior.pitch    = 3;
        interior.distance = 1.5f;
        interior.target   = {-1.35f, -.47850394f, -.25f};
        visual.SetCamera(interior);
        auto final     = ao;
        final.output   = AOOutput::Composite;
        final.method   = AOMethod::HBAO;
        final.denoise  = true;
        final.strength = 1.5f;
        final.radius   = .08f;
        final.samples  = 16;
        visual.SetAmbientOcclusion(final);
        const auto picture = visual.DrawFrame(480, 320);
        if (argc > 3)
        {
            const auto    outPath = std::filesystem::path(argv[3]).parent_path() / "ao-native-view.rgba";
            std::ofstream out(outPath, std::ios::binary);
            out.write(reinterpret_cast<const char*>(&picture.width), 4);
            out.write(reinterpret_cast<const char*>(&picture.height), 4);
            out.write(reinterpret_cast<const char*>(picture.pixels.data()), picture.pixels.size());
        }
        final.method   = AOMethod::RTAO;
        final.strength = 0;
        visual.SetAmbientOcclusion(final);
        const auto zero = visual.DrawFrame(32, 32);
        final.method    = AOMethod::Disabled;
        visual.SetAmbientOcclusion(final);
        if (zero.pixels != visual.DrawFrame(32, 32).pixels)
            throw std::runtime_error("Zero strength is not disabled AO");
        std::cout << "PASS AO planar invariance, four algorithms, visibility range, occluded corners, camera target "
                     "reset and white Sponza load\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
