#pragma once
#include <array>

namespace render
{
    struct ViewportGridSettings
    {
        bool enabled = false;
        float elevation = 0, minorSpacing = 1, majorSpacing = 10, lineWidth = 1;
        std::array<float, 4> minorColor{.18f, .18f, .18f, .32f};
        std::array<float, 4> majorColor{.24f, .24f, .24f, .65f};
    };
}
