#include "TestDevice.hpp"
#include "path/PathTracing.hpp"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stb_image_write.h>
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
        {
            std::cout << "SKIP " << caps.reason << '\n';
            return 77;
        }
        const uint32_t size  = caps.softwareRayTracing ? 32 : 64;
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
        PathTracingSettings settings;
        settings.samplesPerFrame = 4;
        settings.maxBounces      = 5;
        settings.resolutionScale = 1;
        auto uniforms            = MakeSceneUniforms(*model, camera, 1, LightingMode::PBR, light, nullptr, environment);
        std::filesystem::create_directories(argv[3]);
        std::shared_ptr<const EnvironmentData> sky;
        auto                                   render = [&](bool mis, int spp) {
            settings.lightSampling = mis;
            session.Reset();
            std::vector<float> result;
            for (int i = 0; i < spp; i += 4)
            {
                const auto& image = session.Render(model, sky, uniforms, settings, {0, 0, 0, 1}, size, size);
                if (i + 4 == spp)
                {
                    result.resize(size * size);
                    std::memcpy(result.data(), image.pixels.data(), image.pixels.size());
                }
            }
            for (float x : result)
                if (!std::isfinite(x) || x < 0)
                    throw std::runtime_error("Non-finite radiance");
            return result;
        };
        auto baseline8 = render(false, 8), baseline32 = render(false, 32), mis8 = render(true, 8),
             mis32 = render(true, 32), mis128 = render(true, 128), reference = render(true, 1024),
             baselineReference = render(false, 1024);
        auto mse               = [&](const auto& pixels) {
            double sum   = 0;
            size_t count = 0;
            for (uint32_t y = size / 4; y < size * 9 / 10; y++)
                for (uint32_t x = size / 5; x < size * 4 / 5; x++)
                {
                    const size_t i = y * size + x;
                    sum += std::pow(pixels[i] - reference[i], 2);
                    count++;
                }
            return sum / count;
        };
        auto mean = [&](const auto& pixels) {
            double sum   = 0;
            size_t count = 0;
            for (uint32_t y = size / 4; y < size * 9 / 10; y++)
                for (uint32_t x = size / 5; x < size * 4 / 5; x++)
                {
                    sum += pixels[y * size + x];
                    count++;
                }
            return sum / count;
        };
        std::cout << "Cornell linear luminance MSE: BSDF8=" << mse(baseline8) << " BSDF32=" << mse(baseline32)
                  << " MIS8=" << mse(mis8) << " MIS32=" << mse(mis32) << " MIS128=" << mse(mis128)
                  << " reference=1024spp\n";
        const double energy = std::abs(mean(reference) / mean(baselineReference) - 1);
        std::cout << "1024spp mean: BSDF=" << mean(baselineReference) << " MIS=" << mean(reference)
                  << " relative difference=" << energy << std::endl;
        if (!(mse(mis8) < mse(baseline8) * .6 && mse(mis32) < mse(baseline32) * .6 && mse(mis128) < mse(mis32)
              && energy < .1))
            throw std::runtime_error("MIS convergence or energy comparison failed");
        for (const auto& item :
             {std::pair<const char*, const std::vector<float>*>("bsdf-8", &baseline8),
              {"mis-8", &mis8},
              {"mis-32", &mis32},
              {"reference", &reference}})
        {
            std::vector<unsigned char> rgb(size * size * 3);
            for (size_t i = 0; i < item.second->size(); i++)
                for (int k = 0; k < 3; k++)
                    rgb[i * 3 + k] = uint8_t(255 * std::pow(1 - std::exp(-(*item.second)[i] * 3), 1 / 2.2));
            const auto file = std::filesystem::path(argv[3]) / ("cornell-" + api + "-" + item.first + ".png");
            stbi_write_png(file.string().c_str(), size, size, 3, rgb.data(), size * 3);
        }
        // A setting change must reset automatically, including the new A/B switch.
        settings.lightSampling = true;
        session.Render(model, sky, uniforms, settings, {0, 0, 0, 1}, size, size);
        if (session.Samples() != 4)
            throw std::runtime_error("Light sampling toggle did not reset accumulation");
        // High-dynamic-range environment: a tiny bright texel catches PDF/Jacobian and rotation errors.
        auto diffuse      = std::make_shared<ModelData>(*EmptyModelData());
        diffuse->vertices = {
          {{-10, -10, 0, 1}, {0, 0, 1, 0}, {0, 0, 0, 0}},
          {{10, -10, 0, 1}, {0, 0, 1, 0}, {0, 0, 0, 0}},
          {{0, 10, 0, 1}, {0, 0, 1, 0}, {0, 0, 0, 0}}};
        diffuse->material  = {0, 1, 1, 1};
        model              = diffuse;
        auto           hdr = std::make_shared<EnvironmentData>();
        EnvironmentMip mip;
        mip.size = 16;
        mip.pixels.resize(6 * 16 * 16 * 4);
        for (size_t i = 0; i < mip.pixels.size(); i += 4)
        {
            mip.pixels[i] = mip.pixels[i + 1] = mip.pixels[i + 2] = 0x211f;
            mip.pixels[i + 3]                                     = 0x3c00;
        }
        for (int k = 0; k < 3; k++)
            mip.pixels[((4 * 16 + 8) * 16 + 8) * 4 + k] = 0x5100; // 40.0, +Z
        hdr->mips.push_back(mip);
        sky                   = hdr;
        settings.maxBounces   = 1;
        environment.intensity = 1;
        for (float rotation : {0.f, 90.f})
        {
            environment.rotation = rotation;
            uniforms  = MakeSceneUniforms(*model, camera, 1, LightingMode::PBR, light, sky.get(), environment);
            auto bsdf = render(false, 8), importance = render(true, 8);
            reference               = render(true, 1024);
            auto         brute      = render(false, 1024);
            const double difference = std::abs(mean(reference) / mean(brute) - 1);
            std::cout << "HDR rotation=" << rotation << " BSDF8 MSE=" << mse(bsdf) << " MIS8 MSE=" << mse(importance)
                      << " mean difference=" << difference << std::endl;
            if (!(mse(importance) < mse(bsdf) * .6 && difference < .1))
                throw std::runtime_error("HDR sampling/rotation/energy failed");
        }
        std::cout << "PASS emissive-only Cornell importance sampling, MIS, convergence, energy and reset\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
