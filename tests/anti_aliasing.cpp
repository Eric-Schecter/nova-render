#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <render/AntiAliasingView.hpp>
#include <stdexcept>
int main(int argc, char** argv)
{
    try
    {
        render::RendererConfig config;
        const std::string      api            = argc > 1 ? argv[1] : "metal";
        config.api                            = api == "metal"    ? render::GraphicsAPI::Metal
                                                : api == "vulkan" ? render::GraphicsAPI::Vulkan
                                                                  : render::GraphicsAPI::D3D12;
        config.useWarp                        = api == "dx12";
        const std::filesystem::path directory = argc > 2 ? argv[2] : "build/validation";
        std::filesystem::create_directories(directory);
        render::AntiAliasingView     view(config);
        render::AntiAliasingSettings settings;
        if (!settings.textureFiltering)
            throw std::runtime_error("Filtering must default on");
        settings.textureFiltering          = false;
        settings.split                     = false;
        const char*                names[] = {"off", "fxaa", "smaa", "msaa", "ssaa", "taa"};
        std::vector<unsigned char> off;
        std::ofstream              json(directory / ("aa-" + api + ".json"));
        json << "[";
        for (unsigned i = 0; i < 6; i++)
        {
            settings.method = static_cast<render::AAMethod>(i);
            view.SetAntiAliasing(settings);
            if (view.GetHistoryFrames() != 0)
                throw std::runtime_error("Mode switch did not reset history");
            render::Image image;
            for (unsigned f = 0; f < (i == 5 ? 8u : 1u); f++)
                image = view.DrawFrame(128, 96);
            if (image.width != 128 || image.height != 96 || image.pixels.size() != 128 * 96 * 4)
                throw std::runtime_error("AA image dimensions invalid");
            if (view.GetHistoryFrames() != (i == 5 ? 8u : 0u))
                throw std::runtime_error("AA frame counter invalid");
            if (i == 0)
                off = image.pixels;
            double difference = 0;
            for (size_t p = 0; p < image.pixels.size(); p += 4)
                for (int c = 0; c < 3; c++)
                    difference += std::abs(int(image.pixels[p + c]) - int(off[p + c]));
            difference /= 128 * 96 * 3;
            if (i > 0 && difference < .1)
                throw std::runtime_error(std::string("AA did not modify edges: ") + names[i]);
            std::ofstream ppm(directory / (std::string("aa-") + api + "-" + names[i] + ".ppm"), std::ios::binary);
            ppm << "P6\n128 96\n255\n";
            for (size_t p = 0; p < image.pixels.size(); p += 4)
                ppm.write(reinterpret_cast<const char*>(image.pixels.data() + p), 3);
            if (i)
                json << ",";
            json << "[";
            for (size_t p = 0; p < image.pixels.size(); p++)
            {
                if (p)
                    json << ",";
                json << unsigned(image.pixels[p]);
            }
            json << "]";
            std::cout << names[i] << ": change=" << difference << ", history=" << view.GetHistoryFrames() << '\n';
        }
        json << "]";
        view.SetTime(2);
        if (view.GetHistoryFrames() != 0)
            throw std::runtime_error("Time cut did not reset history");
        view.DrawFrame(128, 96);
        auto camera = view.GetCamera();
        camera.yaw += 40;
        view.SetCamera(camera);
        if (view.GetHistoryFrames() != 0)
            throw std::runtime_error("Camera cut did not reset history");
        view.DrawFrame(64, 48);
        if (view.GetHistoryFrames() != 1)
            throw std::runtime_error("Resize history invalid");
        settings.zoom = 0;
        bool rejected = false;
        try
        {
            view.SetAntiAliasing(settings);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Invalid settings accepted");
        std::cout << "PASS raster AA modes, real multisampling, reconstruction, history cuts/resize and validation\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
