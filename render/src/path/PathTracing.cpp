#include "PathTracing.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace render::detail
{
    void ValidatePathTracing(const PathTracingSettings& settings)
    {
        if (
          settings.samplesPerFrame < 1 || settings.samplesPerFrame > 4 || settings.maxBounces < 1
          || settings.maxBounces > 8 || !std::isfinite(settings.resolutionScale) || settings.resolutionScale < 0.25f
          || settings.resolutionScale > 1)
            throw std::invalid_argument("Path tracing requires samples 1..4, bounces 1..8 and scale 0.25..1");
    }
    RendererCapabilities PathSession::Capabilities(GraphicsAPI api, const RendererConfig& config)
    {
        if (!queried)
        {
            queried = true;
            try
            {
                switch (api)
                {
#if defined(__APPLE__) && defined(NOVA_HAS_PATH_TRACING)
                case GraphicsAPI::Metal: device = CreateMetalPathDevice(config); break;
#endif
#if defined(_WIN32) && defined(NOVA_HAS_PATH_TRACING)
                case GraphicsAPI::D3D12: device = CreateD3D12PathDevice(config); break;
#endif
#if defined(NOVA_HAS_VULKAN) && defined(NOVA_HAS_PATH_TRACING)
                case GraphicsAPI::Vulkan: device = CreateVulkanPathDevice(config); break;
#endif
                default: throw std::runtime_error("Path tracing unavailable on this backend");
                }
                capabilities = device->capabilities;
            }
            catch (const std::exception& error)
            {
                capabilities.reason = error.what();
            }
        }
        return capabilities;
    }
    const ModelImage& PathSession::Render(
      std::shared_ptr<const ModelData>       model,
      std::shared_ptr<const EnvironmentData> environment,
      const SceneUniforms&                   scene,
      const PathTracingSettings&             settings,
      const std::array<float, 4>&            clear,
      uint32_t                               width,
      uint32_t                               height)
    {
        if (!device || !capabilities.pathTracing)
            throw std::runtime_error("Path tracing device unavailable");
        if (!model || model->vertices.empty())
            throw std::runtime_error("Load a model before path tracing");
        if (model != currentModel || environment != currentEnvironment)
        {
            const auto& sky = BackgroundMips(environment ? *environment : FallbackEnvironment()).front();
            PathScene   data{*model, sky, {}, {}};
            for (size_t i = 0; i < 5; ++i)
            {
                const auto& texture = model->images[i];
                data.images[i]      = {uint32_t(data.texels.size() / 4), texture.width, texture.height, 0};
                data.texels.insert(data.texels.end(), texture.pixels.begin(), texture.pixels.end());
            }
            device->SetScene(data);
            imageInfo          = data.images;
            currentModel       = model;
            currentEnvironment = environment;
            samples            = 0;
        }
        PathUniforms params{};
        params.scene  = scene;
        params.images = imageInfo;
        params.clear  = clear;
        params.frame  = {
          0,
          std::max(1u, uint32_t(width * settings.resolutionScale)),
          std::max(1u, uint32_t(height * settings.resolutionScale)),
          settings.maxBounces};
        if (uint64_t(params.frame[1]) * params.frame[2] > 16u * 1024u * 1024u)
            throw std::invalid_argument("Path image exceeds 16 million pixels; reduce resolution scale");
        params.options = {
          settings.samplesPerFrame,
          BackgroundMips(environment ? *environment : FallbackEnvironment()).front().size,
          0,
          0};
        if (std::memcmp(&params, &previous, sizeof(params)) || samples > 1000000)
        {
            samples  = 0;
            previous = params;
        }
        params.frame[0] = samples;
        auto pixels     = device->Trace(params);
        image.width     = params.frame[1];
        image.height    = params.frame[2];
        image.pixels    = std::move(pixels);
        samples += settings.samplesPerFrame;
        return image;
    }
} // namespace render::detail
