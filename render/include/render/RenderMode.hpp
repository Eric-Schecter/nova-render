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
        PhotonMapping
    };
    struct PathTracingSettings
    {
        uint32_t       samplesPerFrame     = 1;
        uint32_t       maxBounces          = 3;
        float          resolutionScale     = 0.5f;
        bool           denoise             = false;
        bool           lightSampling       = true; // PT next-event estimation. BDPT always uses MIS.
        uint32_t       photonsPerIteration = 8192;
        float          photonRadius        = .16f;
        PathIntegrator integrator          = PathIntegrator::PathTracing;
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
    };
} // namespace render
