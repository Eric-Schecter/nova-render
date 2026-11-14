#pragma once
#include <cstdint>
#include <vector>
namespace render
{
    // Display-ready sRGB RGBA8 pixels. Independent of windowing and graphics API.
    struct Image
    {
        uint32_t                   width = 1, height = 1;
        std::vector<unsigned char> pixels;
    };
} // namespace render
