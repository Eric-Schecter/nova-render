#include "TestDevice.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <render/ShadowView.hpp>
#include <stdexcept>
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
        const std::filesystem::path out = argc > 2 ? argv[2] : "build/validation";
        std::filesystem::create_directories(out);
        render::ShadowView           view(config);
        render::AntiAliasingSettings aa;
        view.SetAntiAliasing(aa);
        auto camera     = view.GetCamera();
        camera.yaw      = 28;
        camera.pitch    = 30;
        camera.distance = 8;
        view.SetCamera(camera);
        std::ofstream json(out / ("shadow-" + api + ".json"));
        json << "[";
        std::vector<render::Image> images;
        for (unsigned method = 0; method < 4; method++)
        {
            auto settings   = view.GetShadowSettings();
            settings.method = render::ShadowMethod(method);
            view.SetShadowSettings(settings);
            auto image = view.DrawFrame(256, 192);
            if (image.pixels.size() != 256 * 192 * 4)
                throw std::runtime_error("Invalid image size");
            std::ofstream ppm(out / ("shadow-" + api + "-" + std::to_string(method) + ".ppm"), std::ios::binary);
            ppm << "P6\n256 192\n255\n";
            if (method)
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
            images.push_back(image);
        }
        json << "]";
        auto diff = [](const render::Image& a, const render::Image& b) {
            double sum = 0;
            for (size_t i = 0; i < a.pixels.size(); i++)
                sum += std::abs(int(a.pixels[i]) - int(b.pixels[i]));
            return sum / a.pixels.size();
        };
        for (unsigned i = 1; i < 4; i++)
        {
            double change = diff(images[0], images[i]);
            if (change < .1)
                throw std::runtime_error("Shadow did not change scene");
            double darkening = 0;
            for (size_t j = 0; j < images[i].pixels.size(); j += 4)
                darkening += int(images[0].pixels[j]) - int(images[i].pixels[j]);
            if (darkening < 100)
                throw std::runtime_error("Shadow did not reduce direct light");
            std::cout << "Shadow " << i << " Off difference " << change << "\n";
        }
        if (diff(images[1], images[2]) < .01 || diff(images[2], images[3]) < .01)
            throw std::runtime_error("Filtering modes identical");
        for (unsigned method = 0; method < 6; method++)
        {
            aa.method = render::AAMethod(method);
            view.SetAntiAliasing(aa);
            view.DrawFrame(128, 96);
            view.DrawFrame(128, 96);
            if (method == 5 && view.GetHistoryFrames() != 2)
                throw std::runtime_error("TAA history invalid");
        }
        camera.yaw += 2;
        view.SetCamera(camera);
        view.DrawFrame(128, 96);
        if (view.GetHistoryFrames() != 3)
            throw std::runtime_error("Camera motion lost history");
        camera.yaw += 40;
        view.SetCamera(camera);
        if (view.GetHistoryFrames() != 0)
            throw std::runtime_error("Camera cut retained history");
        auto sh = view.GetShadowSettings();
        for (auto size : {512u, 2048u})
        {
            sh.resolution = size;
            view.SetShadowSettings(sh);
            view.DrawFrame(128, 96);
        }
        sh.resolution = 123;
        bool rejected = false;
        try
        {
            view.SetShadowSettings(sh);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Invalid resolution accepted");
        std::cout << "PASS four shadow modes, map resize, six AA methods, TAA/cuts and invalid settings\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
