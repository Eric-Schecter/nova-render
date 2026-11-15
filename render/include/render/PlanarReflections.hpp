#pragma once
namespace render
{
    struct PlanarReflectionSettings
    {
        bool  floorEnabled = true, mirrorEnabled = true;
        float floorRoughness = .06f, mirrorRoughness = .02f;
        float floorIntensity = 1, mirrorIntensity = 1;
    };
} // namespace render
