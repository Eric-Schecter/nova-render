#pragma once
#include <cstdint>
namespace render
{
    enum class ReflectionDebug
    {
        Composite,
        Reflections,
        HitMask,
        Normals,
        Roughness,
        LinearDepth,
        DepthPyramid
    };
    struct ScreenSpaceReflectionSettings
    {
        bool            enabled      = true;
        bool            hierarchical = true;
        uint32_t        maxSteps     = 128;
        float           maxDistance = 24, thickness = .12f, stride = 1;
        float           roughnessCutoff = .8f, edgeFade = .08f, intensity = 1, environmentIntensity = 1;
        ReflectionDebug debug    = ReflectionDebug::Composite;
        uint32_t        debugMip = 3;
    };
} // namespace render
