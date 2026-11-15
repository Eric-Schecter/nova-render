#pragma once
#include <array>
#include <cstdint>
#include <tuple>
#include <vector>
namespace render
{
    enum class ProbeDebug
    {
        Composite,
        Reflections,
        Influence,
        Cubemap
    };
    struct ReflectionProbeVolume
    {
        bool                 enabled   = true;
        float                intensity = 1, blendDistance = .8f;
        std::array<float, 3> position{0, .6f, 0}, boxMin{-4, -1, -4}, boxMax{4, 3, 4};
        std::array<float, 3> influenceMin{-4, -1, -4}, influenceMax{4, 3, 4};
        bool                 operator==(const ReflectionProbeVolume& o) const
        {
            return std::tie(enabled, intensity, blendDistance, position, boxMin, boxMax, influenceMin, influenceMax)
                   == std::tie(
                     o.enabled,
                     o.intensity,
                     o.blendDistance,
                     o.position,
                     o.boxMin,
                     o.boxMax,
                     o.influenceMin,
                     o.influenceMax);
        }
        bool operator!=(const ReflectionProbeVolume& o) const { return !(*this == o); }
    };
    struct ReflectionProbeSettings
    {
        bool                               enabled = true, boxProjection = true, smoothBlending = true;
        float                              roughness = .12f, intensity = 1, blendDistance = 0;
        std::array<float, 3>               position{0, .6f, 0};
        std::array<float, 3>               boxMin{-4, -1, -4}, boxMax{4, 3, 4};
        ProbeDebug                         debug = ProbeDebug::Composite;
        std::vector<ReflectionProbeVolume> probes; // Empty preserves the legacy single-probe fields.
        uint32_t                           debugProbe = 0;
    };
} // namespace render
