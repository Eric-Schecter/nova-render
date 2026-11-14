#pragma once
#include <cmath>
#include <render/Window.hpp>
#include <stdexcept>
#include <string>

namespace example
{
    struct ModelOptions
    {
        render::WindowConfig        window;
        std::string                 modelPath;
        std::string                 environmentPath;
        render::DirectionalLight    light;
        render::RenderMode          mode = render::RenderMode::Raster;
        render::PathTracingSettings path;
        int                         frameLimit = 0;
        bool                        spin       = false; // Optional automatic orbit for fixed-frame validation.
    };

    inline ModelOptions ParseModelOptions(
      int         argc,
      char**      argv,
      const char* title,
      bool        allowLighting      = false,
      bool        allowEnvironment   = false,
      bool        defaultEnvironment = false)
    {
        if (argc < (allowEnvironment ? 3 : 2))
            throw std::invalid_argument(
              "Usage: example model.glb [environment.nenv for environment example] [--api metal|vulkan|dx12] [--warp] "
              "[--frames N] [--spin] [--environment path.nenv] "
              "[--light-direction X Y Z] [--light-color R G B] [--light-intensity N] [--mode raster|ray|compare] "
              "[--spp N] "
              "[--bounces N] [--scale N] [--integrator path|bidirectional|photon-mapping] [--compare-samples N | "
              "--compare-ms N]");
        ModelOptions options;
        options.modelPath = argv[1];
        if (defaultEnvironment)
            options.light.intensity = 0;
        if (allowEnvironment)
        {
            options.environmentPath = argv[2];
            options.light.intensity = 0;
        }
        options.window.title  = title;
        options.window.width  = 960;
        options.window.height = 720;
        auto number           = [](const char* text) {
            size_t            used  = 0;
            const std::string value = text;
            const float       n     = std::stof(value, &used);
            if (used != value.size())
                throw std::invalid_argument("Invalid light number");
            return n;
        };

        for (int i = allowEnvironment ? 3 : 2; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--api" && i + 1 < argc)
            {
                const std::string api = argv[++i];
                if (api == "metal")
                    options.window.renderer.api = render::GraphicsAPI::Metal;
                else if (api == "vulkan")
                    options.window.renderer.api = render::GraphicsAPI::Vulkan;
                else if (api == "dx12")
                    options.window.renderer.api = render::GraphicsAPI::D3D12;
                else
                    throw std::invalid_argument("Unknown API");
            }
            else if (arg == "--environment" && (allowEnvironment || defaultEnvironment) && i + 1 < argc)
                options.environmentPath = argv[++i];
            else if (arg == "--frames" && i + 1 < argc)
            {
                size_t            consumed = 0;
                const std::string n        = argv[++i];
                options.frameLimit         = std::stoi(n, &consumed);
                if (options.frameLimit <= 0 || consumed != n.size())
                    throw std::invalid_argument("Invalid frame count");
            }
            else if (allowLighting && (arg == "--light-direction" || arg == "--light-color") && i + 3 < argc)
            {
                auto& values = arg == "--light-direction" ? options.light.direction : options.light.color;
                for (float& value : values)
                    value = number(argv[++i]);
            }
            else if (allowLighting && arg == "--light-intensity" && i + 1 < argc)
                options.light.intensity = number(argv[++i]);
            else if (arg == "--mode" && i + 1 < argc)
            {
                const std::string mode = argv[++i];
                if (mode == "raster")
                    options.mode = render::RenderMode::Raster;
                else if (mode == "compare")
                    options.mode = render::RenderMode::IntegratorComparison;
                else if (mode == "ray" || mode == "path")
                    options.mode = render::RenderMode::PathTracing;
                else
                    throw std::invalid_argument("Unknown render mode");
            }
            else if (arg == "--integrator" && i + 1 < argc)
            {
                const std::string integrator = argv[++i];
                if (integrator == "path")
                    options.path.integrator = render::PathIntegrator::PathTracing;
                else if (integrator == "bidirectional")
                    options.path.integrator = render::PathIntegrator::Bidirectional;
                else if (integrator == "photon-mapping")
                    options.path.integrator = render::PathIntegrator::PhotonMapping;
                else
                    throw std::invalid_argument("Unknown path integrator: " + integrator);
            }
            else if ((arg == "--spp" || arg == "--bounces") && i + 1 < argc)
            {
                const float value = number(argv[++i]);
                if (!std::isfinite(value) || value < 1 || value > 8 || std::floor(value) != value)
                    throw std::invalid_argument("Invalid path integer");
                if (arg == "--spp")
                    options.path.samplesPerFrame = uint32_t(value);
                else
                    options.path.maxBounces = uint32_t(value);
            }
            else if ((arg == "--compare-samples" || arg == "--compare-ms") && i + 1 < argc)
            {
                const float value = number(argv[++i]);
                if (
                  !std::isfinite(value) || value < 1 || value > (arg == "--compare-ms" ? 60000 : 65536)
                  || std::floor(value) != value)
                    throw std::invalid_argument("Invalid comparison budget");
                if (arg == "--compare-ms")
                    options.path.comparisonMilliseconds = uint32_t(value);
                else
                {
                    options.path.comparisonSamples      = uint32_t(value);
                    options.path.comparisonMilliseconds = 0;
                }
            }
            else if (arg == "--scale" && i + 1 < argc)
                options.path.resolutionScale = number(argv[++i]);
            else if (arg == "--warp")
                options.window.renderer.useWarp = true;
            else if (arg == "--spin")
                options.spin = true;
            else
                throw std::invalid_argument("Unknown argument: " + arg);
        }
        if (defaultEnvironment && options.environmentPath.empty())
            options.environmentPath =
              (std::filesystem::path(options.modelPath).parent_path().parent_path() / "Environment/venice_sunset.nenv")
                .string();
        return options;
    }
} // namespace example
