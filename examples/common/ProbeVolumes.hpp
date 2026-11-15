#pragma once
#include <render/ReflectionProbe.hpp>
namespace example
{
    inline std::vector<render::ReflectionProbeVolume> RoomProbes()
    {
        std::vector<render::ReflectionProbeVolume> probes(3);
        for (int i = 0; i < 3; i++)
        {
            auto& p         = probes[i];
            p.position      = {-2.5f + i * 2.5f, .6f, -1};
            p.blendDistance = 1.2f;
            p.influenceMin  = {i == 0 ? -4.f : i == 1 ? -1.5f : -.75f, -2, -5};
            p.influenceMax  = {i == 0 ? .75f : i == 1 ? 1.5f : 4.f, 4, 5};
        }
        return probes;
    }
} // namespace example
