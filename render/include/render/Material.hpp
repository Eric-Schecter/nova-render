#pragma once
#include "Image.hpp"
#include <array>
#include <filesystem>
#include <optional>
#include <string>
namespace render
{
    // Opaque glTF metallic/roughness material. Color factors are linear; image bytes are RGBA8.
    struct Material
    {
        std::string          name;
        std::array<float, 4> baseColor{1, 1, 1, 1};
        float                roughness = 1, metallic = 1, normalScale = 1, occlusionStrength = 1;
        std::array<float, 3> emissive{0, 0, 0};
        // Base color and emissive are sRGB; the other three maps are linear.
        std::optional<Image> baseColorTexture, normalTexture, metallicRoughnessTexture, occlusionTexture,
          emissiveTexture;
    };
    Image LoadMaterialImage(const std::filesystem::path& path);
} // namespace render
