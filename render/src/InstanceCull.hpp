#pragma once
#include "ModelData.hpp"
#include <algorithm>

namespace render::detail
{
    struct CullBounds
    {
        std::array<float, 4> center{}, extent{};
    };
    struct CullParameters
    {
        float    planes[6][4]{};
        uint32_t instanceCount, unused, dispatchWidth, pad;
    };
    static_assert(sizeof(CullBounds) == 32 && sizeof(CullParameters) == 112);

    inline CullBounds InstanceCullBounds(const ModelData& model, uint32_t first, uint32_t count)
    {
        if (!count)
            return {};
        const auto*          vertices = model.vertices.data() + first;
        std::array<float, 3> minimum{}, maximum{};
        std::copy_n(vertices->position, 3, minimum.begin());
        maximum        = minimum;
        bool triangles = true;
        for (uint32_t v = 0; v < count; ++v)
        {
            triangles &= vertices[v].uv1[2] <= .5f;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                minimum[axis] = std::min(minimum[axis], vertices[v].position[axis]);
                maximum[axis] = std::max(maximum[axis], vertices[v].position[axis]);
            }
        }
        CullBounds box;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            box.center[axis] = (maximum[axis] + minimum[axis]) * .5f;
            box.extent[axis] = (maximum[axis] - minimum[axis]) * .5f;
        }
        // Lines and points expand in clip space. Keep those batches conservative.
        box.extent[3] = triangles ? 1.f : 0.f;
        return box;
    }
    inline CullParameters InstanceCullPlanes(const SceneUniforms& uniforms)
    {
        CullParameters params{};
        for (uint32_t axis = 0; axis < 4; ++axis)
        {
            // Extract clip-space inequalities from the columns of world-to-clip.
            const float* column    = uniforms.viewProjection + axis * 4;
            params.planes[0][axis] = column[3] + column[0];
            params.planes[1][axis] = column[3] - column[0];
            params.planes[2][axis] = column[3] + column[1];
            params.planes[3][axis] = column[3] - column[1];
            params.planes[4][axis] = column[2];
            params.planes[5][axis] = column[3] - column[2];
        }
        return params;
    }
} // namespace render::detail
