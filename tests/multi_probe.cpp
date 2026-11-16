#include "../examples/common/ProbeVolumes.hpp"
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
        const std::string      api = argc > 1 ? TestAPI(argv[1]) : "metal";
        render::RendererConfig config;
        config.api     = api == "metal"    ? render::GraphicsAPI::Metal
                         : api == "vulkan" ? render::GraphicsAPI::Vulkan
                                           : render::GraphicsAPI::D3D12;
        config.useWarp = TestUseWarp(argv[1]);
        render::ReflectionProbeView  view(config);
        render::AntiAliasingSettings aa;
        view.SetAntiAliasing(aa);
        auto post  = view.GetPostProcessing();
        post.bloom = false;
        view.SetPostProcessing(post);
        auto settings   = view.GetReflectionProbe();
        settings.probes = example::RoomProbes();
        view.SetReflectionProbe(settings);
        const std::filesystem::path dir = argc > 2 ? argv[2] : "build/validation";
        std::filesystem::create_directories(dir);
        std::ofstream json(dir / ("multi-probe-" + api + ".json"));
        json << "[";
        std::vector<render::Image> images;
        for (int mode = 0; mode < 8; mode++)
        {
            settings.debug          = mode < 3    ? render::ProbeDebug::Composite
                                      : mode < 6  ? render::ProbeDebug::Influence
                                      : mode == 6 ? render::ProbeDebug::Reflections
                                                  : render::ProbeDebug::Cubemap;
            settings.debugProbe     = mode == 7 ? 2 : 0;
            settings.smoothBlending = mode != 6;
            view.SetReflectionProbe(settings);
            view.SetTime(mode % 3 == 0 ? 4.712389f : mode % 3 == 1 ? 0.f : 1.5707963f);
            images.push_back(view.DrawFrame(256, 192));
            if (mode)
                json << ",";
            json << "[";
            const auto& image = images.back();
            for (size_t i = 0; i < image.pixels.size(); i++)
            {
                if (i)
                    json << ",";
                json << unsigned(image.pixels[i]);
            }
            json << "]";
        }
        json << "]";
        if (view.GetProbeCaptureCount() != 3)
            throw std::runtime_error("Animation must reuse three probe captures");
        auto difference = [](const render::Image& a, const render::Image& b) {
            double sum = 0;
            for (size_t i = 0; i < a.pixels.size(); i++)
                sum += std::abs(int(a.pixels[i]) - int(b.pixels[i]));
            return sum / a.pixels.size();
        };
        if (difference(images[0], images[2]) < .5)
            throw std::runtime_error("Object failed to traverse probe regions");
        size_t mixed = 0;
        for (size_t i = 0; i < images[4].pixels.size(); i += 4)
            if (images[4].pixels[i] > 80 && images[4].pixels[i + 1] > 80 && images[4].pixels[i + 2] > 80)
                mixed++;
        if (mixed < 100)
            throw std::runtime_error("Missing smoothly blended influence region");
        settings.debug                 = render::ProbeDebug::Composite;
        settings.debugProbe            = 0;
        settings.smoothBlending        = true;
        settings.probes[1].position[1] = .8f;
        view.SetReflectionProbe(settings);
        view.DrawFrame(128, 96);
        if (view.GetProbeCaptureCount() != 4)
            throw std::runtime_error("Moving one probe recaptured unrelated probes");
        settings.probes[0].intensity     = .6f;
        settings.probes[1].blendDistance = .5f;
        view.SetReflectionProbe(settings);
        view.DrawFrame(128, 96);
        if (view.GetProbeCaptureCount() != 4)
            throw std::runtime_error("Blend/intensity changes recaptured probes");
        view.UpdateProbe(2);
        view.DrawFrame(128, 96);
        if (view.GetProbeCaptureCount() != 5)
            throw std::runtime_error("Indexed probe refresh failed");
        for (int method = 0; method < 6; method++)
        {
            aa.method = render::AAMethod(method);
            view.SetAntiAliasing(aa);
            view.DrawFrame(137, 93);
            view.DrawFrame(137, 93);
        }
        if (view.GetProbeCaptureCount() != 5)
            throw std::runtime_error("Resize or AA lost cubemap cache");
        settings.probes.push_back(settings.probes[0]);
        settings.probes[3].position = {1, .6f, 1};
        view.SetReflectionProbe(settings);
        view.DrawFrame(137, 93);
        if (view.GetProbeCaptureCount() != 6)
            throw std::runtime_error("Fourth probe not independently captured");
        auto bad = settings;
        bad.probes.push_back(bad.probes[0]);
        bool rejected = false;
        try
        {
            view.SetReflectionProbe(bad);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected || view.GetReflectionProbes().size() != 4)
            throw std::runtime_error("Invalid probe count mutated settings");
        settings.probes.assign(3, render::ReflectionProbeVolume{});
        for (auto& p : settings.probes)
            p.blendDistance = 0;
        aa.method = render::AAMethod::Off;
        view.SetAntiAliasing(aa);
        view.SetTime(0);
        view.SetReflectionProbe(settings);
        const auto all             = view.DrawFrame(128, 96);
        settings.probes[1].enabled = settings.probes[2].enabled = false;
        view.SetReflectionProbe(settings);
        const auto one = view.DrawFrame(128, 96);
        if (difference(all, one) > .02)
            throw std::runtime_error("Overlapping probes multiplied reflected energy");
        for (auto& p : settings.probes)
        {
            p.influenceMin = {10, 10, 10};
            p.influenceMax = {11, 11, 11};
        }
        view.SetReflectionProbe(settings);
        const auto outside = view.DrawFrame(128, 96);
        settings.enabled   = false;
        view.SetReflectionProbe(settings);
        if (difference(outside, view.DrawFrame(128, 96)) > .01)
            throw std::runtime_error("Missing outside-region environment fallback");
        post.autoExposure = true;
        post.bloom        = true;
        view.SetPostProcessing(post);
        view.DrawFrame(1, 1);
        view.DrawFrame(137, 93);
        std::cout << "PASS three/four probes, crossing, normalized blending, independent captures, six AA, HDR post "
                     "and fallback\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
