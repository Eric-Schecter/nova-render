#include "Picking.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <stdexcept>
namespace render::detail
{
    namespace
    {
        PickVector point(const ModelVertex& v)
        {
            return {v.position[0], v.position[1], v.position[2]};
        }
        PickVector sub(PickVector a, PickVector b)
        {
            return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
        }
        PickVector cross(PickVector a, PickVector b)
        {
            return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
        }
        double dot(PickVector a, PickVector b)
        {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        }
        double length(PickVector v)
        {
            return std::hypot(v[0], v[1], v[2]);
        }
        bool box(PickVector low, PickVector high, PickVector o, PickVector d, double limit)
        {
            double near = 0, far = limit;
            for (int k = 0; k < 3; k++)
            {
                const double pad = 1e-12 * std::max({1., std::abs(low[k]), std::abs(high[k])});
                if (d[k] == 0)
                {
                    if (o[k] < low[k] - pad || o[k] > high[k] + pad)
                        return false;
                }
                else
                {
                    double a = (low[k] - pad - o[k]) / d[k], b = (high[k] + pad - o[k]) / d[k];
                    if (a > b)
                        std::swap(a, b);
                    near = std::max(near, a);
                    far  = std::min(far, b);
                    if (far < near)
                        return false;
                }
            }
            return true;
        }
    } // namespace
    PickMesh::PickMesh(std::shared_ptr<const ModelData> data)
      : data_(std::move(data))
    {
        if (!data_ || data_->vertices.size() % 3 || data_->vertices.size() / 3 > std::numeric_limits<uint32_t>::max())
            throw std::invalid_argument("Invalid picking geometry");
        for (uint32_t i = 0; i < data_->vertices.size() / 3; ++i)
            if (data_->vertices[size_t(i) * 3].uv1[2] == 0)
                triangles_.push_back(i);
        if (!triangles_.empty())
            Build(0, uint32_t(triangles_.size()));
    }
    uint32_t PickMesh::Build(uint32_t first, uint32_t count)
    {
        Node node;
        node.first = first;
        node.count = count;
        node.minimum.fill(std::numeric_limits<double>::infinity());
        node.maximum.fill(-std::numeric_limits<double>::infinity());
        for (uint32_t i = first; i < first + count; i++)
            for (int j = 0; j < 3; j++)
            {
                const auto p = point(data_->vertices[size_t(triangles_[i]) * 3 + j]);
                for (int k = 0; k < 3; k++)
                {
                    node.minimum[k] = std::min(node.minimum[k], p[k]);
                    node.maximum[k] = std::max(node.maximum[k], p[k]);
                }
            }
        const auto index = uint32_t(nodes_.size());
        nodes_.push_back(node);
        if (count > 8)
        {
            int axis = 0;
            for (int k = 1; k < 3; k++)
                if (node.maximum[k] - node.minimum[k] > node.maximum[axis] - node.minimum[axis])
                    axis = k;
            auto centroid = [&](uint32_t t) {
                double value = 0;
                for (int j = 0; j < 3; j++)
                    value += data_->vertices[size_t(t) * 3 + j].position[axis];
                return value;
            };
            std::sort(triangles_.begin() + first, triangles_.begin() + first + count, [&](uint32_t a, uint32_t b) {
                auto x = centroid(a), y = centroid(b);
                return x != y ? x < y : a < b;
            });
            const auto left = Build(first, count / 2), right = Build(first + count / 2, count - count / 2);
            nodes_[index].left  = left;
            nodes_[index].right = right;
            nodes_[index].count = 0;
        }
        return index;
    }
    std::vector<std::array<float, 48>> PickMesh::ShadowRecords() const
    {
        std::vector<std::array<float, 48>> records(nodes_.size() + triangles_.size());
        if (records.size() * 192ull > 64ull * 1024 * 1024)
            throw std::runtime_error("Scene shadow BVH exceeds 64 MiB");
        std::function<void(uint32_t, uint32_t)> escape = [&](uint32_t index, uint32_t next) {
            const auto& node   = nodes_[index];
            auto&       record = records[index];
            for (int k = 0; k < 3; k++)
            {
                record[32 + k] = float(node.minimum[k]);
                record[36 + k] = float(node.maximum[k]);
            }
            record[35] = float(next);
            record[44] = float(node.left);
            record[45] = float(node.right);
            record[46] = float(nodes_.size() + node.first);
            record[47] = float(node.count);
            if (!node.count)
            {
                escape(node.left, node.right);
                escape(node.right, next);
            }
        };
        if (!nodes_.empty())
            escape(0, uint32_t(nodes_.size()));
        for (size_t t = 0; t < triangles_.size(); t++)
        {
            auto& record = records[nodes_.size() + t];
            record[6] = data_->vertices[size_t(triangles_[t]) * 3].normal[3];
            for (int j = 0; j < 3; j++)
            {
                const auto& vertex = data_->vertices[size_t(triangles_[t]) * 3 + j];
                for (int k = 0; k < 3; k++)
                    record[32 + j * 4 + k] = vertex.position[k];
                if (j < 2)
                {
                    record[44 + j * 2] = vertex.uv[0];
                    record[45 + j * 2] = vertex.uv[1];
                }
                else
                {
                    record[16] = vertex.uv[0];
                    record[21] = vertex.uv[1];
                }
                const int colorOffset = j < 2 ? 8 + j * 4 : 24;
                std::copy_n(vertex.color, 4, record.begin() + colorOffset);
                record[j * 2]     = vertex.uv1[0];
                record[j * 2 + 1] = vertex.uv1[1];
                record[43] = vertex.position[3] - 1;
            }
        }
        return records;
    }
    std::optional<MeshHit> PickMesh::Pick(const PickVector& o, const PickVector& d, double limit) const
    {
        if (nodes_.empty())
            return {};
        std::optional<MeshHit> best;
        std::vector<uint32_t>  stack{0};
        while (!stack.empty())
        {
            const auto& node = nodes_[stack.back()];
            stack.pop_back();
            if (!box(node.minimum, node.maximum, o, d, best ? best->distance : limit))
                continue;
            if (!node.count)
            {
                stack.push_back(node.right);
                stack.push_back(node.left);
                continue;
            }
            for (uint32_t i = node.first; i < node.first + node.count; i++)
            {
                const auto triangle = triangles_[i];
                const auto a        = point(data_->vertices[size_t(triangle) * 3]),
                           e1       = sub(point(data_->vertices[size_t(triangle) * 3 + 1]), a),
                           e2       = sub(point(data_->vertices[size_t(triangle) * 3 + 2]), a);
                const auto   n = cross(e1, e2), p = cross(d, e2);
                const double determinant = dot(e1, p);
                if (std::abs(determinant) <= 1e-12 * length(n) * length(d))
                    continue;
                const auto   t = sub(o, a);
                const double u = dot(t, p) / determinant;
                if (u < -1e-12 || u > 1 + 1e-12)
                    continue;
                const auto   q = cross(t, e1);
                const double v = dot(d, q) / determinant;
                if (v < -1e-12 || u + v > 1 + 1e-12)
                    continue;
                const double distance = dot(e2, q) / determinant;
                if (!std::isfinite(distance) || distance < 0 || distance > limit)
                    continue;
                if (!best || distance < best->distance || (distance == best->distance && triangle < best->triangle))
                    best = MeshHit{distance, n, triangle};
            }
        }
        return best;
    }
} // namespace render::detail
