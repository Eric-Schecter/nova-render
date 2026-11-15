#pragma once
#include "ModelData.hpp"
namespace render::detail
{
    // Color slots (base/emissive) are averaged in linear light. Data slots stay linear.
    std::vector<ModelImage> MaterialMipmaps(const ModelImage& image, size_t slot);
} // namespace render::detail
