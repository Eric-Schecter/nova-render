#pragma once
#include <cstdint>
#include <string>
namespace render
{
    enum class HybridView
    {
        Final,
        Shadows,
        AmbientOcclusion
    };
    struct HybridSettings
    {
        bool       shadows       = true;
        uint32_t   shadowSamples = 4;
        HybridView view          = HybridView::Final;
        uint32_t   shadowLight   = 0;
        bool       ambientOcclusion = false;
        float      aoRadius = 1.f, aoStrength = 1.f;
        uint32_t   aoSamples = 4;
    };
    struct HybridCapabilities
    {
        bool        supported = true, rasterPrimaryVisibility = true, shadows = true;
        bool        softwareRayTracing = true, hardwareRayTracing = false;
        bool        ambientOcclusion = true, reflections = false, globalIllumination = false;
        bool        temporalReconstruction = false, animatedGeometry = false, taa = false, splitAA = false;
        bool        gpuPresentation = true, alphaMask = true, alphaBlend = true;
        std::string execution = "fragment-bvh", reason;
    };
} // namespace render
