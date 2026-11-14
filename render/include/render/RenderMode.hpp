#pragma once
#include <cstdint>
#include <string>
namespace render
{
    enum class RenderMode
    {
        Raster,
        RayTracing,
        PathTracing = RayTracing, // Compatibility alias.
        IntegratorComparison
    };
    enum class PathIntegrator
    {
        PathTracing,
        Bidirectional,
        PhotonMapping,
        Metropolis
    };
    struct PathTracingSettings
    {
        uint32_t       samplesPerFrame         = 1;
        uint32_t       maxBounces              = 3;
        float          resolutionScale         = 0.5f;
        bool           denoise                 = false;
        bool           lightSampling           = true; // PT next-event estimation. BDPT always uses MIS.
        PathIntegrator integrator              = PathIntegrator::PathTracing;
        uint32_t       photonsPerIteration     = 8192;
        float          photonRadius            = .16f;
        uint32_t       comparisonSamples       = 64;
        uint32_t       comparisonMilliseconds  = 0; // 0: fixed spp; otherwise active render time per method.
        uint32_t       mltBootstrapSamples     = 262144;
        float          mltLargeStepProbability = .3f;
        float          mltMutationScale        = .01f;
        uint32_t       mltSeed                 = 1;
    };
    using RayTracingSettings = PathTracingSettings;
    using RayIntegrator      = PathIntegrator;
    struct ComparisonProgress
    {
        PathIntegrator integrator   = PathIntegrator::PathTracing;
        uint32_t       samples      = 0;
        double         milliseconds = 0;
        bool           complete     = false;
    };
    struct RendererCapabilities
    {
        bool        pathTracing        = false;
        bool        softwareRayTracing = false;
        std::string rayTracingDevice;
        std::string reason;
        bool        rayTracing = false;
    };
} // namespace render
