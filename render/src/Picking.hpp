#pragma once
#include "ModelData.hpp"
#include <array>
#include <optional>
#include <vector>
namespace render::detail
{
    using PickVector = std::array<double, 3>;
    struct MeshHit
    {
        double     distance;
        PickVector normal;
        uint32_t   triangle;
    };
    // Immutable asset-local BVH. Geometry stays shared with the renderer's model data.
    class PickMesh
    {
      public:
        explicit PickMesh(std::shared_ptr<const ModelData> data);
        std::optional<MeshHit> Pick(const PickVector& origin, const PickVector& direction, double limit) const;

      private:
        struct Node
        {
            PickVector minimum{}, maximum{};
            uint32_t   first = 0, count = 0, left = 0, right = 0;
        };
        uint32_t                         Build(uint32_t first, uint32_t count);
        std::shared_ptr<const ModelData> data_;
        std::vector<uint32_t>            triangles_;
        std::vector<Node>                nodes_;
    };
} // namespace render::detail
