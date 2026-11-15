#include "PathTracing.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace render::detail
{
    void ValidateAmbientOcclusion(const AmbientOcclusionSettings& s)
    {
        if (
          int(s.method) < 0 || int(s.method) > 4 || int(s.output) < 0 || int(s.output) > 3 || !std::isfinite(s.radius)
          || s.radius < .001f || s.radius > 1 || !std::isfinite(s.bias) || s.bias < 0 || s.bias > .05f
          || !std::isfinite(s.strength) || s.strength < 0 || s.strength > 4 || s.samples < 4 || s.samples > 64
          || !std::isfinite(s.resolutionScale) || s.resolutionScale < .25f || s.resolutionScale > 1)
            throw std::invalid_argument("Invalid ambient occlusion settings");
    }

    void ValidatePathTracing(const PathTracingSettings& settings)
    {
        if (
          settings.integrator != PathIntegrator::PathTracing && settings.integrator != PathIntegrator::Bidirectional
          && settings.integrator != PathIntegrator::PhotonMapping && settings.integrator != PathIntegrator::Metropolis)
            throw std::invalid_argument("Unknown path integrator");
        if (
          settings.mltBootstrapSamples < 256 || settings.mltBootstrapSamples > 1048576
          || !std::isfinite(settings.mltLargeStepProbability) || settings.mltLargeStepProbability < .05f
          || settings.mltLargeStepProbability > 1 || !std::isfinite(settings.mltMutationScale)
          || settings.mltMutationScale < .0001f || settings.mltMutationScale > .25f)
            throw std::invalid_argument(
              "MLT requires bootstrap 256..1048576, large-step probability .05..1 and mutation scale .0001...25");
        if (
          settings.photonsPerIteration < 256 || settings.photonsPerIteration > 65536
          || !std::isfinite(settings.photonRadius) || settings.photonRadius < .03f || settings.photonRadius > .5f)
            throw std::invalid_argument("Photon mapping requires 256..65536 photons and radius .03...5");
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
      uint32_t                               height,
      const AmbientOcclusionSettings*        ao)
    {
        ValidatePathTracing(settings);
        if (ao)
            ValidateAmbientOcclusion(*ao);
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
            PrepareLightSampling(data, bool(environment));
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
        if (ao && ao->enabled)
        {
            params.frame[1] = std::max(1u, uint32_t(float(width) * ao->resolutionScale));
            params.frame[2] = std::max(1u, uint32_t(float(height) * ao->resolutionScale));
        }
        if (uint64_t(params.frame[1]) * params.frame[2] > 16u * 1024u * 1024u)
            throw std::invalid_argument("Path image exceeds 16 million pixels; reduce resolution scale");
        params.options = {
          settings.samplesPerFrame,
          BackgroundMips(environment ? *environment : FallbackEnvironment()).front().size,
          0,
          (settings.denoise ? 1u : 0u)
            | ((settings.lightSampling || settings.integrator != PathIntegrator::PathTracing) ? 2u : 0u)
            | (linearOutput ? 4u : 0u) | (settings.integrator == PathIntegrator::Bidirectional ? 8u : 0u)
            | (settings.integrator == PathIntegrator::PhotonMapping ? 16u : 0u)
            | (settings.integrator == PathIntegrator::Metropolis ? 32u : 0u)};
        std::memcpy(&params.images[1][3], &settings.photonRadius, 4);
        params.images[2][3] = settings.photonsPerIteration;
        if (settings.integrator == PathIntegrator::Metropolis)
        {
            std::memcpy(&params.clear[3], &settings.mltBootstrapSamples, 4);
            std::memcpy(&params.scene.skyParams[2], &settings.mltSeed, 4);
            std::memcpy(&params.images[3][3], &settings.mltLargeStepProbability, 4);
            std::memcpy(&params.images[4][3], &settings.mltMutationScale, 4);
        }
        if (ao && ao->enabled)
        {
            params.options[0] = 1;
            params.options[3] = 64u | (ao->denoise ? 1u : 0u) | (linearOutput ? 4u : 0u);
            params.clear      = {ao->radius, ao->bias, ao->strength, 0};
            const auto method = uint32_t(ao->method);
            std::memcpy(&params.clear[3], &method, 4);
            params.images[1][3] = uint32_t(ao->output);
            params.images[2][3] = ao->samples;
            params.images[3][3] = ao->seed;
        }
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
        samples += params.options[0];
        return image;
    }
} // namespace render::detail
