#pragma once
#include <cstdint>
#include <string>
namespace render
{
    enum class HybridView
    {
        Final,
        Shadows
    };
    struct HybridSettings
    {
        bool       shadows       = true;
        uint32_t   shadowSamples = 4;
        HybridView view          = HybridView::Final;
        uint32_t   shadowLight   = 0;
    };
    struct HybridCapabilities
    {
        bool        supported = true, rasterPrimaryVisibility = true, shadows = true;
        bool        softwareRayTracing = true, hardwareRayTracing = false;
        bool        ambientOcclusion = false, reflections = false, globalIllumination = false;
        bool        temporalReconstruction = false, animatedGeometry = false, taa = false, splitAA = false;
        bool        gpuPresentation = true, alphaMask = true, alphaBlend = true;
        std::string execution = "fragment-bvh", reason;
    };
} // namespace render
