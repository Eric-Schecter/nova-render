#pragma once
#include <array>
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
    };

    struct SceneAsset
    {
        std::string           id;
        std::filesystem::path path;
        // false: centered, longest axis spans [-1, 1] (legacy LoadModel behavior).
        // true: retain glTF scene units, origin and authored node transforms.
        bool sourceSpace = false;
    };

    struct SceneInstance
    {
        // Instance IDs are unique within a scene; asset IDs refer to SceneAsset::id.
        std::string id, assetId;
        // Maps the selected asset space into world space. Identity by default.
        Matrix4 world{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    };
} // namespace render
