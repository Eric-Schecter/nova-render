#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <render/PipelineComparisonView.hpp>
void Save(const render::Image& image, const std::filesystem::path& file)
{
    std::ofstream out(file, std::ios::binary);
    out << "P6\n" << image.width << " " << image.height << "\n255\n";
    for (size_t i = 0; i < image.pixels.size(); i += 4)
        out.write(reinterpret_cast<const char*>(image.pixels.data() + i), 3);
}
double Difference(const render::Image& a, const render::Image& b)
{
    double d = 0;
    for (size_t i = 0; i < a.pixels.size(); i++)
        d += std::abs(int(a.pixels[i]) - int(b.pixels[i]));
    return d / a.pixels.size();
}
int main(int argc, char** argv)
{
    try
    {
        std::string            api = argc > 1 ? argv[1] : "metal";
        render::RendererConfig config;
        config.api = api == "metal"    ? render::GraphicsAPI::Metal
                     : api == "vulkan" ? render::GraphicsAPI::Vulkan
                                       : render::GraphicsAPI::D3D12;
        render::PipelineComparisonView     view(config);
        render::PipelineComparisonSettings settings;
        auto                               dir = std::filesystem::path("build/validation/pipelines") / api;
        std::filesystem::create_directories(dir);
        for (int stress = 0; stress < 3; stress++)
        {
            settings.lightCount  = stress == 0 ? 128 : stress == 1 ? 256 : 0;
            settings.lightRadius = stress == 1 ? 32 : 3;
            render::Image reference;
            for (int mode = 0; mode < 4; mode++)
            {
                settings.pipeline = render::RasterPipeline(mode);
                settings.debug    = render::PipelineDebug::Final;
                view.SetSettings(settings);
                auto image = view.DrawFrame(321, 181);
                Save(image, dir / (std::to_string(stress) + "-" + std::to_string(mode) + ".ppm"));
                if (mode == 0)
                    reference = image;
                else
                {
                    double d = Difference(image, reference);
                    std::cout << api << " stress " << stress << " pipeline " << mode << " difference " << d << "\n";
                    if (d > .15)
                        throw std::runtime_error("Pipeline parity failed");
                }
            }
        }
        settings.lightCount  = 128;
        settings.lightRadius = 3;
        for (int debug = 1; debug < 8; debug++)
        {
            render::Image reference;
            for (int mode = 0; mode < 4; mode++)
            {
                settings.pipeline = render::RasterPipeline(mode);
                settings.debug    = render::PipelineDebug(debug);
                view.SetSettings(settings);
                auto image = view.DrawFrame(321, 181);
                Save(image, dir / ("debug-" + std::to_string(debug) + "-" + std::to_string(mode) + ".ppm"));
                if (mode == 0)
                    reference = image;
                else if (debug < 6 && Difference(reference, image) > .15)
                    throw std::runtime_error("G-buffer diagnostic parity failed");
            }
        }
        settings.pipeline   = render::RasterPipeline::Forward;
        settings.debug      = render::PipelineDebug::Final;
        settings.lightCount = 128;
        view.SetSettings(settings);
        auto lit            = view.DrawFrame(321, 181);
        settings.lightCount = 0;
        view.SetSettings(settings);
        if (Difference(lit, view.DrawFrame(321, 181)) < 2)
            throw std::runtime_error("Lights must visibly change the image");
        settings.lightCount = 128;
        settings.debug      = render::PipelineDebug::LightCount;
        view.SetSettings(settings);
        auto allLights = view.DrawFrame(321, 181);
        for (auto pipeline : {render::RasterPipeline::ForwardPlus, render::RasterPipeline::ClusteredForward})
        {
            settings.pipeline = pipeline;
            view.SetSettings(settings);
            if (Difference(allLights, view.DrawFrame(321, 181)) < 2)
                throw std::runtime_error("Spatial culling must reduce candidate counts");
            settings.debug       = render::PipelineDebug::Overflow;
            settings.lightCount  = 256;
            settings.lightRadius = 32;
            view.SetSettings(settings);
            const auto image    = view.DrawFrame(321, 181);
            bool       overflow = false;
            for (size_t i = 0; i < image.pixels.size(); i += 4)
                overflow |= image.pixels[i] > 240 && image.pixels[i + 1] < 10 && image.pixels[i + 2] > 240;
            if (!overflow)
                throw std::runtime_error("Overflow fixture must exercise fallback");
            settings.debug       = render::PipelineDebug::LightCount;
            settings.lightCount  = 128;
            settings.lightRadius = 3;
        }
        auto camera     = view.GetCamera();
        camera.yaw      = -65;
        camera.pitch    = 38;
        camera.distance = 14;
        camera.target   = {1, 1, 1};
        view.SetCamera(camera);
        settings.lightCount  = 96;
        settings.lightRadius = 2.5f;
        settings.debug       = render::PipelineDebug::Final;
        for (auto size : {std::array<uint32_t, 2>{16, 9}, {127, 65}, {641, 359}})
        {
            settings.pipeline = render::RasterPipeline::Forward;
            view.SetSettings(settings);
            auto reference = view.DrawFrame(size[0], size[1]);
            for (auto pipeline : {render::RasterPipeline::ForwardPlus, render::RasterPipeline::ClusteredForward})
            {
                settings.pipeline = pipeline;
                view.SetSettings(settings);
                if (Difference(reference, view.DrawFrame(size[0], size[1])) > .15)
                    throw std::runtime_error("Resized/camera compute parity failed");
            }
        }
        auto s        = view.GetSettings();
        s.lightCount  = 257;
        bool rejected = false;
        try
        {
            view.SetSettings(s);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Invalid count accepted");
        std::cout << "Pipeline image parity, diagnostics, zero lights, overflow fallback and validation passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
