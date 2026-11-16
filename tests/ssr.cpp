#include "TestDevice.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <render/ScreenSpaceReflectionView.hpp>
#include <stdexcept>
int main(int argc, char** argv)
{
    try
    {
        render::RendererConfig config;
        const std::string      api = argc > 1 ? TestAPI(argv[1]) : "metal";
        config.api                 = api == "metal"    ? render::GraphicsAPI::Metal
                                     : api == "vulkan" ? render::GraphicsAPI::Vulkan
                                                       : render::GraphicsAPI::D3D12;
        config.useWarp             = TestUseWarp(argv[1]);
        std::filesystem::path dir  = argc > 2 ? argv[2] : "build/validation";
        std::filesystem::create_directories(dir);
        render::ScreenSpaceReflectionView view(config);
        render::AntiAliasingSettings      aa;
        view.SetAntiAliasing(aa);
        auto pp  = view.GetPostProcessing();
        pp.bloom = false;
        view.SetPostProcessing(pp);
        auto                       settings = view.GetScreenSpaceReflections();
        std::vector<render::Image> images;
        std::ofstream              json(dir / ("ssr-" + api + ".json"));
        json << "[";
        for (unsigned mode = 0; mode < 7; mode++)
        {
            settings.enabled = mode != 0;
            settings.debug   = mode < 2 ? render::ReflectionDebug::Composite : render::ReflectionDebug(mode);
            view.SetScreenSpaceReflections(settings);
            auto image = view.DrawFrame(256, 192);
            images.push_back(image);
            std::ofstream ppm(dir / ("ssr-" + api + "-" + std::to_string(mode) + ".ppm"), std::ios::binary);
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
        auto diff = [](const render::Image& a, const render::Image& b) {
            double sum = 0;
            for (size_t i = 0; i < a.pixels.size(); i++)
                sum += std::abs(int(a.pixels[i]) - int(b.pixels[i]));
            return sum / a.pixels.size();
        };
        if (diff(images[0], images[1]) < .03)
            throw std::runtime_error("SSR did not change reflection");
        size_t hits = 0;
        for (size_t i = 0; i < images[2].pixels.size(); i += 4)
            if (images[2].pixels[i] > 50)
                hits++;
        if (hits < 30)
            throw std::runtime_error("Missing SSR hits");
        std::cout << "SSR difference " << diff(images[0], images[1]) << "; hit pixels " << hits << "\n";
        settings.debug        = render::ReflectionDebug::Composite;
        settings.hierarchical = false;
        view.SetScreenSpaceReflections(settings);
        auto linear = view.DrawFrame(256, 192);
        std::cout << "Linear/Hi-Z MAE " << diff(linear, images[1]) << "\n";
        if (diff(linear, images[1]) > 1)
            throw std::runtime_error("Hi-Z disagrees with linear traversal");
        settings.hierarchical = true;
        view.SetScreenSpaceReflections(settings);
        for (unsigned mode = 0; mode < 6; mode++)
        {
            aa.method = render::AAMethod(mode);
            view.SetAntiAliasing(aa);
            view.DrawFrame(128, 96);
            view.DrawFrame(128, 96);
            if (mode == 5 && view.GetHistoryFrames() != 2)
                throw std::runtime_error("SSR TAA history failed");
        }
        view.SetTime(.1f);
        view.DrawFrame(128, 96);
        if (view.GetHistoryFrames() != 3)
            throw std::runtime_error("Animated SSR lost TAA history");
        if (argc > 3)
        {
            settings.debug   = render::ReflectionDebug::Reflections;
            settings.enabled = false;
            view.SetScreenSpaceReflections(settings);
            aa.method = render::AAMethod::Off;
            view.SetAntiAliasing(aa);
            view.LoadEnvironment(std::filesystem::path(argv[3]) / "venice_sunset.nenv");
            auto a = view.DrawFrame(128, 96);
            view.LoadEnvironment(std::filesystem::path(argv[3]) / "studio_small_09.nenv");
            auto b = view.DrawFrame(128, 96);
            if (diff(a, b) < .1)
                throw std::runtime_error("Environment fallback unchanged");
            view.UseDefaultEnvironment();
        }
        settings.enabled = true;
        settings.debug   = render::ReflectionDebug::Composite;
        view.SetScreenSpaceReflections(settings);
        aa.method = render::AAMethod::SSAA;
        view.SetAntiAliasing(aa);
        pp.autoExposure = true;
        pp.bloom        = true;
        view.SetPostProcessing(pp);
        view.DrawFrame(137, 93);
        view.DrawFrame(1, 1);
        settings.debug    = render::ReflectionDebug::DepthPyramid;
        settings.debugMip = 12;
        view.SetScreenSpaceReflections(settings);
        view.DrawFrame(1, 1);
        view.DrawFrame(137, 93);
        bool rejected = false;
        try
        {
            settings.thickness = -1;
            view.SetScreenSpaceReflections(settings);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Invalid SSR settings accepted");
        std::cout << "PASS SSR hits, environment fallback, depth/normal/roughness diagnostics, hierarchy, six AA, HDR "
                     "post and resizing\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
