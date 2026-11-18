#pragma once
#include "Image.hpp"
#include <array>
#include <filesystem>
#include <optional>
#include <string>
namespace render
{
    enum class AlphaMode
    {
        Opaque,
        Mask,
        Blend
    };
    struct MaterialCapabilities
    {
        bool        opaque = true, mask = true, blend = true, doubleSided = true;
        bool        maskShadows = true, blendShadows = false;
        bool        maskRayTracing = false, blendRayTracing = false, rayMaterialSidedness = false;
        bool        alphaAwarePicking = false;
        bool        blendMSAA = false, blendTAA = false, blendAASplit = false;
        const char* transparency = "weighted-blended-oit";
    };
    // glTF metallic/roughness material. Color factors are linear; image bytes are RGBA8.
    struct Material
    {
        std::string          name;
        AlphaMode            alphaMode   = AlphaMode::Opaque;
        float                alphaCutoff = .5f;
        bool                 doubleSided = false;
        std::array<float, 4> baseColor{1, 1, 1, 1};
        float                roughness = 1, metallic = 1, normalScale = 1, occlusionStrength = 1;
        std::array<float, 3> emissive{0, 0, 0};
        // Base color and emissive are sRGB; the other three maps are linear.
        std::optional<Image> baseColorTexture, normalTexture, metallicRoughnessTexture, occlusionTexture,
          emissiveTexture;
        float                specularFactor = 1;
        std::array<float, 3> specularColorFactor{1, 1, 1};
    };
    Image LoadMaterialImage(const std::filesystem::path& path);
} // namespace render
