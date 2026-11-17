#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace render
{
    struct AnimationClip
    {
        std::string name;
        float       duration = 0;
    };
    struct AnimationCapabilities
    {
        bool skeletal = true, nodeTRS = true, morph = false, instancePlayback = true, gpuSkinning = true,
             manualClock           = true;
        bool        dynamicShadows = false, posedPicking = true, posedBounds = true, rayTracing = false, hybrid = false;
        float       minimumSpeed = .1f, maximumSpeed = 4.f;
        std::string reason;
    };
    struct AnimationState
    {
        uint32_t clip    = 0;
        float    time    = 0;
        float    speed   = 1;
        bool     playing = true;
        bool     loop    = true;
    };
} // namespace render
