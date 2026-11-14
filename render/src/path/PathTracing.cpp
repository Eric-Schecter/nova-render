#include "PathTracing.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace render::detail
{
    void ValidatePathTracing(const PathTracingSettings& settings)
    {
        if (
          settings.comparisonSamples < 1 || settings.comparisonSamples > 65536
          || settings.comparisonMilliseconds > 60000)
            throw std::invalid_argument("Comparison requires 1..65536 spp or 1..60000 ms");
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
            queried          = true;
            comparisonAPI    = api;
            comparisonConfig = config;
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
    const ModelImage& PathSession::RenderComparison(
      std::shared_ptr<const ModelData> model,
      const SceneUniforms&             input,
      const PathTracingSettings&       settings,
      uint32_t                         width,
      uint32_t                         height)
    {
        ValidatePathTracing(settings);
        SceneUniforms scene = input;
        // Square views, emitter-only illumination and no display denoising.
        scene.cameraRight[3]       = scene.cameraUp[3];
        scene.cameraForward[3]     = 0;
        scene.environmentParams[2] = 0;
        scene.skyParams[0] = scene.skyParams[1] = 0;
        scene.lightColorIntensity[3]            = 0;
        const bool     vertical                 = height > width;
        const uint32_t side = std::max(1u, vertical ? std::min(width, height / 4) : std::min(width / 4, height));
        const std::array<float, 14> key{
          float(width),
          float(height),
          float(side),
          float(settings.samplesPerFrame),
          float(settings.maxBounces),
          settings.resolutionScale,
          float(settings.photonsPerIteration),
          settings.photonRadius,
          float(settings.comparisonSamples),
          float(settings.comparisonMilliseconds),
          float(settings.mltBootstrapSamples),
          settings.mltLargeStepProbability,
          settings.mltMutationScale,
          float(settings.mltSeed)};
        if (
          comparisonDirty || comparisonModel != model.get() || key != comparisonKey
          || std::memcmp(&scene, &comparisonScene, sizeof(scene)))
        {
            comparisonDirty = false;
            comparisonModel = model.get();
            comparisonKey   = key;
            comparisonScene = scene;
            for (size_t i = 0; i < 4; ++i)
            {
                if (!comparisons[i])
                {
                    comparisons[i]  = std::make_unique<PathSession>(linearOutput);
                    const auto caps = comparisons[i]->Capabilities(comparisonAPI, comparisonConfig);
                    if (!caps.pathTracing)
                        throw std::runtime_error(caps.reason);
                }
                comparisons[i]->Reset();
                auto warm            = settings;
                warm.samplesPerFrame = 1;
                warm.integrator      = static_cast<PathIntegrator>(i);
                warm.denoise         = false;
                warm.lightSampling   = true;
                comparisons[i]->Render(model, nullptr, scene, warm, {0, 0, 0, 1}, side, side);
                comparisons[i]->Reset();
                comparisonProgress[i] = {static_cast<PathIntegrator>(i), 0, 0, false};
            }
        }
        const uint32_t panel   = std::max(1u, uint32_t(side * settings.resolutionScale));
        comparisonImage.width  = std::max(panel * (vertical ? 1 : 4), uint32_t(width * settings.resolutionScale));
        comparisonImage.height = std::max(panel * (vertical ? 4 : 1), uint32_t(height * settings.resolutionScale));
        comparisonImage.pixels.assign(size_t(comparisonImage.width) * comparisonImage.height * 4, 0);
        if (!linearOutput)
            for (size_t a = 3; a < comparisonImage.pixels.size(); a += 4)
                comparisonImage.pixels[a] = 255;
        for (size_t i = 0; i < 4; ++i)
        {
            auto& progress       = comparisonProgress[i];
            auto  next           = settings;
            next.integrator      = static_cast<PathIntegrator>(i);
            next.denoise         = false;
            next.lightSampling   = true;
            next.samplesPerFrame = 1;
            if (!progress.complete)
            {
                const auto start = std::chrono::steady_clock::now();
                comparisons[i]->Render(model, nullptr, scene, next, {0, 0, 0, 1}, side, side);
                progress.milliseconds +=
                  std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                progress.samples  = comparisons[i]->Samples();
                progress.complete = settings.comparisonMilliseconds
                                      ? progress.milliseconds >= settings.comparisonMilliseconds
                                      : progress.samples >= settings.comparisonSamples;
            }
            const auto& frame = comparisons[i]->image;
            const auto  top   = (comparisonImage.height - panel * (vertical ? 4 : 1)) / 2 + (vertical ? i * panel : 0);
            const auto  left  = (comparisonImage.width - panel * (vertical ? 1 : 4)) / 2 + (vertical ? 0 : i * panel);
            for (uint32_t y = 0; y < panel; ++y)
                std::memcpy(
                  comparisonImage.pixels.data() + (size_t(y + top) * comparisonImage.width + left) * 4,
                  frame.pixels.data() + size_t(y) * panel * 4,
                  size_t(panel) * 4);
        }
        samples = std::min(
          {comparisonProgress[0].samples,
           comparisonProgress[1].samples,
           comparisonProgress[2].samples,
           comparisonProgress[3].samples});
        return comparisonImage;
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
        ValidatePathTracing(settings);
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
