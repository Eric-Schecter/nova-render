#pragma once
#include <cstdint>
#include <string>
namespace render
{
    enum class RenderMode
    {
        Raster,
        RayTracing,
        PathTracing = RayTracing // Compatibility alias.
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
        uint32_t       mltBootstrapSamples     = 262144;
        float          mltLargeStepProbability = .3f;
        float          mltMutationScale        = .01f;
        uint32_t       mltSeed                 = 1;
    };
    using RayTracingSettings = PathTracingSettings;
    using RayIntegrator      = PathIntegrator;
    struct RendererCapabilities
    {
        bool        pathTracing        = false;
        bool        softwareRayTracing = false;
        std::string rayTracingDevice;
        std::string reason;
        bool        rayTracing = false;
        bool        metropolis = false; // MLT support; DX12 WARP is excluded.
    };
} // namespace render
