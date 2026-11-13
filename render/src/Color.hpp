#pragma once
#include <cmath>
namespace render::detail
{
    // UI/background colors are display (sRGB) values; sRGB render targets encode on store.
    inline float SRGBToLinear(float value)
    {
        return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
    }
} // namespace render::detail
