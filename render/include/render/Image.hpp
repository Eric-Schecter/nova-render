#pragma once
#include <cstdint>
#include <vector>
namespace render
{
    // RGBA8 pixels. Color space is defined by the receiving API; presented images use sRGB.
    struct Image
    {
        uint32_t                   width = 1, height = 1;
        std::vector<unsigned char> pixels;
    };
} // namespace render
