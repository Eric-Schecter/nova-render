#pragma once
#include <cstdint>
namespace render
{
    // Raster scene light shadows, evaluated on the GPU using shared per-asset BVHs.
    struct SceneShadowSettings
    {
        bool     enabled  = false;
        float    softness = 1;
        uint32_t samples  = 8;
        float    bias     = .002f;
    };
} // namespace render
