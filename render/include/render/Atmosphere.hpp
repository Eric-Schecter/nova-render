#pragma once
#include <array>
namespace render
{
    enum class FogMode
    {
        Off,
        Linear,
        ExponentialSquared
    };
    struct FogSettings
    {
        FogMode              mode = FogMode::Off;
        std::array<float, 3> color{.3185468f, .3185468f, .3185468f};
        float                nearDistance = 10, farDistance = 50, density = .02f;
    };
} // namespace render
