#pragma once
#include "render/Material.hpp"
#include <array>
#include <memory>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace render
{
    // Column-major affine transform: world = parentWorld * local.
    using Matrix4 = std::array<float, 16>;

    struct Bounds
    {
        std::array<float, 3> minimum{}, maximum{};
    };

    struct Ray
    {
        std::array<float, 3> origin{0, 0, 0}, direction{0, 0, -1};
        // Instance IDs. Empty includeIds means all; exclusions take precedence.
        std::vector<std::string> includeIds, excludeIds;
    };
    struct SceneHit
    {
        std::string          id;
        double               distance = 0;         // World-space distance, independent of direction magnitude.
        std::array<float, 3> position{}, normal{}; // World point and unit geometric normal (world winding).
        uint32_t triangleIndex = 0; // Triangle in the asset's prepared vertex stream.
        std::array<float, 3> barycentric{};
        std::array<float, 2> uv{}; // Interpolated primary UV before sampler wrapping.
    };

    struct SceneVertex
    {
        std::array<float, 3> position{}, normal{0, 1, 0};
        std::array<float, 2> uv{};
    };

    struct SceneGeometry
    {
        // Non-indexed triangle list. One optional zero-based material slot per triangle.
        std::vector<SceneVertex> vertices;
        std::vector<Material> materials;
        std::vector<uint32_t> triangleMaterials;
    };

    struct SceneAsset
    {
        std::string           id;
        std::filesystem::path path;
        // false: centered, longest axis spans [-1, 1] (legacy LoadModel behavior).
        // true: retain glTF scene units, origin and authored node transforms.
        bool sourceSpace = false;
        std::optional<uint32_t> materialVariant;
        // Supply geometry or a path. Publish a new immutable object to replace geometry.
        std::shared_ptr<const SceneGeometry> geometry;
    };

    struct SceneInstance
    {
        // Instance IDs are unique within a scene; asset IDs refer to SceneAsset::id.
        std::string id, assetId;
        // Maps the selected asset space into world space. Identity by default.
        Matrix4 world{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        bool    castShadow = true;
    };

    struct OutlineStyle
    {
        std::array<float, 3> color{};
        uint32_t width = 2;
    };

    struct SceneOutlines
    {
        std::string hoveredId;
        std::vector<std::string> selectedIds;
        OutlineStyle hovered{{.35f, .65f, 1.f}, 2};
        OutlineStyle selected{{1.f, .42f, .08f}, 2};
    };
} // namespace render
