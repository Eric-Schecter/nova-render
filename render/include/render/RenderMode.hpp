#pragma once
#include <cstdint>
#include <string>
namespace render
{
    enum class RenderMode
    {
        Raster,
        PathTracing
    };
    struct PathTracingSettings
    {
        uint32_t samplesPerFrame = 1;
        uint32_t maxBounces      = 3;
        float    resolutionScale = 0.5f;
        bool     denoise         = false;
        bool     lightSampling   = true; // Emissive/HDR next-event estimation with MIS.
    };
    struct RendererCapabilities
    {
        bool        pathTracing        = false;
        bool        softwareRayTracing = false;
        std::string rayTracingDevice;
        std::string reason;
    };
} // namespace render
