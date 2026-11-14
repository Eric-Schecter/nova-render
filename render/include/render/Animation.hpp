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
    struct AnimationState
    {
        uint32_t clip    = 0;
        float    time    = 0;
        float    speed   = 1;
        bool     playing = true;
        bool     loop    = true;
    };
} // namespace render
