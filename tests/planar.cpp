#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <render/PlanarReflectionView.hpp>
int main(int argc, char** argv)
{
    try
    {
        render::RendererConfig config;
        const std::string      api      = argc > 1 ? argv[1] : "metal";
        config.api                      = api == "metal"    ? render::GraphicsAPI::Metal
                                          : api == "vulkan" ? render::GraphicsAPI::Vulkan
                                                            : render::GraphicsAPI::D3D12;
        config.useWarp                  = api == "dx12";
        const std::filesystem::path dir = argc > 2 ? argv[2] : "build/validation";
        std::filesystem::create_directories(dir);
        render::PlanarReflectionView view(config);
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
        std::ofstream              json(dir / ("planar-" + api + ".json"));
        json << "[";
        for (int mode = 0; mode < 6; mode++)
        {
            render::PlanarReflectionSettings s;
            s.floorEnabled  = mode != 0 && mode != 3;
            s.mirrorEnabled = mode != 0 && mode != 2;
            if (mode == 4)
                s.floorRoughness = s.mirrorRoughness = .8f;
            if (mode == 5)
                s.floorIntensity = s.mirrorIntensity = 0;
            view.SetPlanarReflections(s);
            images.push_back(view.DrawFrame(256, 192));
            const auto&   image = images.back();
            std::ofstream ppm(dir / ("planar-" + api + "-" + std::to_string(mode) + ".ppm"), std::ios::binary);
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
        for (int mode : {0, 2, 3, 4, 5})
        {
            const auto d = difference(images[1], images[mode]);
            std::cout << "Planar variant " << mode << " MAE " << d << "\n";
            if (d < .1)
                throw std::runtime_error("Missing independent planar reflection or material effect");
        }
        auto magenta = [](const render::Image& image) {
            size_t count = 0;
            for (size_t i = 0; i < image.pixels.size(); i += 4)
            {
                const auto r = image.pixels[i], g = image.pixels[i + 1], b = image.pixels[i + 2];
                if (r > 80 && r > g * 1.5 && b > g * 1.3 && b > 50)
                    count++;
            }
            return count;
        };
        if (magenta(images[1]) < 5 || magenta(images[0]) != 0 || magenta(images[2]) != 0)
            throw std::runtime_error("Offscreen witness must only appear in vertical planar reflection");
        view.SetPlanarReflections({});
        for (unsigned method = 0; method < 6; method++)
        {
            aa.method = render::AAMethod(method);
            view.SetAntiAliasing(aa);
            view.DrawFrame(128, 96);
            view.DrawFrame(128, 96);
            if (method == 5 && view.GetHistoryFrames() != 2)
                throw std::runtime_error("Planar TAA history lost");
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
        const auto old         = view.GetPlanarReflections();
        auto       invalid     = old;
        invalid.floorRoughness = -1;
        bool rejected          = false;
        try
        {
            view.SetPlanarReflections(invalid);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected || view.GetPlanarReflections().floorRoughness != old.floorRoughness)
            throw std::runtime_error("Invalid planar settings accepted");
        std::cout << "PASS independent mirror/floor, roughness/intensity, six AA, TAA motion, environment, HDR post "
                     "and resizing\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
