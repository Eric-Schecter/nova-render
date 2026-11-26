#include "TestDevice.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <render/PostProcessingView.hpp>
#include <stdexcept>
int main(int argc, char** argv)
{
    try
    {
        render::RendererConfig config;
        std::string            api = argc > 1 ? TestAPI(argv[1]) : "metal";
        config.api                 = api == "metal"    ? render::GraphicsAPI::Metal
                                     : api == "vulkan" ? render::GraphicsAPI::Vulkan
                                                       : render::GraphicsAPI::D3D12;
        config.useWarp             = TestUseWarp(argv[1]);
        std::filesystem::path dir  = argc > 2 ? argv[2] : "build/validation";
        std::filesystem::create_directories(dir);
        render::PostProcessingView   view(config);
        auto                         post = view.GetPostProcessing();
        render::AntiAliasingSettings aa;
        view.SetAntiAliasing(aa);
        std::ofstream json(dir / ("post-" + api + ".json"));
        json << "[";
        std::vector<render::Image> images;
        auto                       draw = [&] { return view.DrawFrame(192, 128); };
        for (int i = 0; i < 6; i++)
        {
            post.toneMapping  = i == 5 ? render::ToneMapping::AgX : render::ToneMapping(std::min(i, 2));
            post.bloom        = i == 3;
            post.saturation   = i == 4 ? 0 : 1;
            post.autoExposure = false;
            view.SetPostProcessing(post);
            auto image = draw();
            images.push_back(image);
            std::ofstream ppm(dir / ("post-" + api + "-" + std::to_string(i) + ".ppm"), std::ios::binary);
            ppm << "P6\n192 128\n255\n";
            if (i)
                json << ",";
            json << "[";
            for (size_t n = 0; n < image.pixels.size(); n++)
            {
                if (n)
                    json << ",";
                json << unsigned(image.pixels[n]);
                if (n % 4 != 3)
                    ppm.put(char(image.pixels[n]));
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
        for (size_t i = 1; i < images.size(); i++)
            if (diff(images[0], images[i]) < .1)
                throw std::runtime_error("Post-processing did not affect image");
        if (diff(images[2], images[3]) < .1)
            throw std::runtime_error("Bloom did not affect image");
        if (diff(images[2], images[5]) < .1)
            throw std::runtime_error("AgX did not change the ACES reference");
        post.toneMapping = render::ToneMapping::ACESFilmic;
        view.SetPostProcessing(post);
        const auto filmic = draw();
        if (diff(filmic, images[2]) < .1 || diff(filmic, images[5]) < .1)
            throw std::runtime_error("ACES Filmic did not apply its own curve");
        post.toneMapping = render::ToneMapping::Linear;
        post.exposureEV  = -4;
        post.bloom       = false;
        post.saturation  = 1;
        view.SetPostProcessing(post);
        const auto hdr  = draw();
        unsigned   peak = 0;
        for (size_t i = 0; i < hdr.pixels.size(); i++)
            if (i % 4 != 3)
                peak = std::max(peak, unsigned(hdr.pixels[i]));
        if (peak < 200)
            throw std::runtime_error("HDR highlights clipped before exposure");
        post.exposureEV   = 0;
        post.toneMapping  = render::ToneMapping::ACES;
        post.saturation   = 1;
        post.bloom        = false;
        post.autoExposure = true;
        view.SetPostProcessing(post);
        auto autoA = draw();
        view.SetSceneIntensity(8);
        auto bright = draw();
        for (int i = 0; i < 12; i++)
            draw();
        auto adapted = draw();
        if (diff(bright, adapted) < .01)
            throw std::runtime_error("Exposure did not adapt");
        for (unsigned method = 0; method < 6; method++)
        {
            aa.method = render::AAMethod(method);
            view.SetAntiAliasing(aa);
            draw();
            draw();
            if (method == 5 && view.GetHistoryFrames() != 2)
                throw std::runtime_error("TAA history failed");
        }
        view.DrawFrame(137, 93);
        view.DrawFrame(1, 1);
        bool rejected = false;
        try
        {
            post.bloomIntensity = -1;
            view.SetPostProcessing(post);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Invalid settings accepted");
        std::cout
          << "PASS HDR curves, bloom, grading, auto exposure/adaptation, six AA, NPOT/tiny resize and validation\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
