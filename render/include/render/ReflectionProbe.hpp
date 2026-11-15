#pragma once
#include <array>
namespace render
{
    enum class ProbeDebug
    {
        Composite,
        Reflections,
        Influence,
        Cubemap
    };
    struct ReflectionProbeSettings
    {
        bool                 enabled = true, boxProjection = true;
        float                roughness = .12f, intensity = 1, blendDistance = 0;
        std::array<float, 3> position{0, .6f, 0};
        std::array<float, 3> boxMin{-4, -1, -4}, boxMax{4, 3, 4};
        ProbeDebug           debug = ProbeDebug::Composite;
    };
} // namespace render
