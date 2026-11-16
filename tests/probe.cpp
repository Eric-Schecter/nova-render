#include "TestDevice.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <render/ReflectionProbeView.hpp>
int main(int argc, char** argv)
{
    try
    {
        render::RendererConfig config;
        const std::string      api      = argc > 1 ? TestAPI(argv[1]) : "metal";
        config.api                      = api == "metal"    ? render::GraphicsAPI::Metal
                                          : api == "vulkan" ? render::GraphicsAPI::Vulkan
                                                            : render::GraphicsAPI::D3D12;
        config.useWarp                  = TestUseWarp(argv[1]);
        const std::filesystem::path dir = argc > 2 ? argv[2] : "build/validation";
        std::filesystem::create_directories(dir);
        render::ReflectionProbeView  view(config);
        render::AntiAliasingSettings aa;
        view.SetAntiAliasing(aa);
        auto post  = view.GetPostProcessing();
        post.bloom = false;
        view.SetPostProcessing(post);
        auto difference = [](const render::Image& a, const render::Image& b) {
            double sum = 0;
            for (size_t i = 0; i < a.pixels.size(); i++)
                sum += std::abs(int(a.pixels[i]) - int(b.pixels[i]));
            return sum / a.pixels.size();
        };
        std::vector<render::Image> images;
        std::ofstream              json(dir / ("probe-" + api + ".json"));
        json << "[";
        for (int mode = 0; mode < 8; mode++)
        {
            render::ReflectionProbeSettings s;
            s.enabled       = mode != 0;
            s.boxProjection = mode != 1;
            if (mode == 3)
                s.roughness = .8f;
            if (mode == 4)
                s.intensity = 0;
            if (mode == 5)
                s.position = {1.3f, .6f, 1.5f};
            if (mode == 6)
                s.debug = render::ProbeDebug::Influence;
            if (mode == 7)
                s.debug = render::ProbeDebug::Cubemap;
            view.SetReflectionProbe(s);
            images.push_back(view.DrawFrame(256, 192));
            const auto&   image = images.back();
            std::ofstream ppm(dir / ("probe-" + api + "-" + std::to_string(mode) + ".ppm"), std::ios::binary);
            ppm << "P6\n256 192\n255\n";
            if (mode)
                json << ",";
            json << "[";
            for (size_t i = 0; i < image.pixels.size(); i++)
            {
                if (i)
                    json << ",";
                json << unsigned(image.pixels[i]);
                if (i % 4 != 3)
                    ppm.put(char(image.pixels[i]));
            }
            json << "]";
        }
        json << "]";
        for (int mode : {0, 1, 3, 4, 5})
        {
            const auto d = difference(images[2], images[mode]);
            std::cout << "Probe variant " << mode << " MAE " << d << "\n";
            if (d < .1)
                throw std::runtime_error("Missing cubemap, box projection or material effect");
        }
        const auto captures = view.GetProbeCaptureCount();
        view.SetReflectionProbe({});
        for (unsigned method = 0; method < 6; method++)
        {
            aa.method = render::AAMethod(method);
            view.SetAntiAliasing(aa);
            view.DrawFrame(128, 96);
            view.DrawFrame(128, 96);
            if (method == 5 && view.GetHistoryFrames() != 2)
                throw std::runtime_error("Probe TAA history lost");
        }
        view.SetTime(.1f);
        view.DrawFrame(128, 96);
        if (view.GetHistoryFrames() != 3)
            throw std::runtime_error("Animation lost history");
        auto camera = view.GetCamera();
        camera.yaw += 2;
        view.SetCamera(camera);
        view.DrawFrame(128, 96);
        if (view.GetHistoryFrames() != 4)
            throw std::runtime_error("Orbit lost history");
        if (view.GetProbeCaptureCount() != captures)
            throw std::runtime_error("AA or camera unnecessarily recaptured probe");
        view.UpdateProbe();
        view.DrawFrame(128, 96);
        if (view.GetProbeCaptureCount() != captures + 1)
            throw std::runtime_error("Explicit refresh failed");
        if (argc > 3)
        {
            view.LoadEnvironment(std::filesystem::path(argv[3]) / "venice_sunset.nenv");
            view.DrawFrame(128, 96);
            view.UseDefaultEnvironment();
        }
        aa.method = render::AAMethod::SSAA;
        view.SetAntiAliasing(aa);
        post.autoExposure = true;
        post.bloom        = true;
        view.SetPostProcessing(post);
        view.DrawFrame(137, 93);
        view.DrawFrame(1, 1);
        view.DrawFrame(137, 93);
        const auto old     = view.GetReflectionProbe();
        auto       invalid = old;
        invalid.roughness  = -1;
        bool rejected      = false;
        try
        {
            view.SetReflectionProbe(invalid);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected || view.GetReflectionProbe().roughness != old.roughness)
            throw std::runtime_error("Invalid probe settings accepted");
        auto outside     = old;
        outside.position = {0, 2, 0};
        outside.boxMin   = {-.2f, 1.8f, -.2f};
        outside.boxMax   = {.2f, 2.2f, .2f};
        aa.method        = render::AAMethod::Off;
        view.SetAntiAliasing(aa);
        post.autoExposure = false;
        view.SetPostProcessing(post);
        view.SetReflectionProbe(outside);
        const auto out  = view.DrawFrame(128, 96);
        outside.enabled = false;
        view.SetReflectionProbe(outside);
        const auto fallback = view.DrawFrame(128, 96);
        if (difference(out, fallback) > .01)
            throw std::runtime_error("Outside probe box did not fall back to environment");
        std::cout << "PASS local cube, box projection, roughness/intensity, capture cache/refresh, six AA, HDR post, "
                     "resizing and outside-box fallback\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
