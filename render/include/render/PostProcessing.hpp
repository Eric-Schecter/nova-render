#pragma once
#include <cstdint>
namespace render
{
    enum class ToneMapping
    {
        Linear,
        Reinhard,
        ACES,
        AgX,
        ACESFilmic
    };
    struct PostProcessingSettings
    {
        bool        enabled      = false;
        ToneMapping toneMapping  = ToneMapping::ACES;
        float       exposureEV   = 0;
        bool        autoExposure = false;
        float       minEV = -6, maxEV = 6, adaptationRate = 1.5f;
        bool        bloom          = true;
        float       bloomThreshold = 1, bloomKnee = .5f, bloomIntensity = .08f;
        float       saturation = 1, contrast = 1;
        bool        toneMapBackground = true;
    };
} // namespace render
