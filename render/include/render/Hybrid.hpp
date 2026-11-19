#pragma once
#include <cstdint>
#include <string>
namespace render
{
    enum class HybridView
    {
        Final,
        Shadows,
        AmbientOcclusion,
        Reflections
    };
    struct HybridSettings
    {
        bool       shadows          = true;
        uint32_t   shadowSamples    = 4;
        HybridView view             = HybridView::Final;
        uint32_t   shadowLight      = 0;
        bool       ambientOcclusion = false;
        float      aoRadius = 1.f, aoStrength = 1.f;
        bool       reflections        = false;
        float      reflectionDistance = 1000.f, reflectionStrength = 1.f;
        uint32_t   reflectionSamples      = 1;
        bool       temporalReconstruction = false;
        float      historyWeight          = .9f;
        uint32_t   aoSamples              = 4;
        bool       diffuseBounce          = false;
        bool       probeGI                = false;
        bool       halfResolutionReflections = false;
    };
    struct HybridCapabilities
    {
        bool        supported = true, rasterPrimaryVisibility = true, shadows = true;
        bool        softwareRayTracing = true, hardwareRayTracing = false;
        bool        ambientOcclusion = true, reflections = true, globalIllumination = true;
        bool        temporalReconstruction = true, animatedGeometry = false, taa = false, splitAA = false;
        bool        probeGI         = true;
        bool        gpuPresentation = true, alphaMask = true, alphaBlend = true;
        std::string execution = "fragment-bvh", reason;
    };
} // namespace render
