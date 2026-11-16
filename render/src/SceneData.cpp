#include "SceneData.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <set>
#include <stdexcept>
namespace render::detail
{
    void SceneData::Update(const std::vector<SceneAsset>& assets, const std::vector<SceneInstance>& input)
    {
        auto next = *this;
        next.Build(assets, input);
        *this = std::move(next);
    }
    void SceneData::Build(const std::vector<SceneAsset>& assets, const std::vector<SceneInstance>& input)
    {
        std::map<std::string, std::filesystem::path> paths;
        for (const auto& a : assets)
            if (a.id.empty() || a.path.empty() || !paths.emplace(a.id, a.path).second)
                throw std::invalid_argument("Scene assets require unique nonempty IDs and paths");
        std::set<std::string> ids;
        std::set<std::string> referenced;
        for (const auto& i : input)
        {
            if (i.id.empty() || !ids.insert(i.id).second || !paths.count(i.assetId))
                throw std::invalid_argument("Invalid scene instance");
            for (float f : i.world)
                if (!std::isfinite(f))
                    throw std::invalid_argument("Invalid instance matrix");
            if (
              std::abs(i.world[3]) > 1e-6 || std::abs(i.world[7]) > 1e-6 || std::abs(i.world[11]) > 1e-6
              || std::abs(i.world[15] - 1) > 1e-6)
                throw std::invalid_argument("Instance matrix must be affine");
            referenced.insert(i.assetId);
        }
        std::vector<std::pair<std::string, std::filesystem::path>> nextKey;
        for (const auto& id : referenced)
            nextKey.emplace_back(id, paths.at(id));
        if (!model || nextKey != key)
        {
            std::map<std::string, Mesh> next;
            uint32_t                    tile = 1;
            for (const auto& entry : nextKey)
            {
                const auto&                      id   = entry.first;
                const auto&                      path = entry.second;
                std::shared_ptr<const ModelData> data;
                auto                             old  = meshes.find(id);
                auto                             prev = std::find_if(
                  key.begin(), key.end(), [&](const auto& p) { return p.first == id && p.second == path; });
                if (old != meshes.end() && prev != key.end())
                    data = old->second.data;
                else
                    data = LoadGLB(path);
                if (data->animation)
                    throw std::invalid_argument("Scene instances currently require static GLB models");
                for (const auto& im : data->images)
                    tile = std::max(tile, std::max(im.width, im.height));
                auto picking = old != meshes.end() && old->second.data == data ? old->second.picking
                                                                               : std::make_shared<PickMesh>(data);
                next[id]     = {data, 0, 0, {}, std::move(picking)};
            }
            // One shared mesh range per asset. Atlas gutter preserves filtering at tile boundaries.
            tile          = std::min(tile, 2048u);
            uint32_t cols = std::max(1u, uint32_t(std::ceil(std::sqrt(double(next.size()))))),
                     rows = std::max(1u, uint32_t((next.size() + cols - 1) / cols)), stride = tile + 4,
                     w = cols * stride, h = rows * stride;
            if (w > 8192 || h > 8192 || uint64_t(w) * h * 20 > 512ull * 1024 * 1024)
                throw std::runtime_error("Scene material atlas exceeds 512 MiB / 8192px limit");
            auto packed = std::make_shared<ModelData>(*EmptyModelData());
            for (auto& image : packed->images)
            {
                image.width  = w;
                image.height = h;
                image.pixels.assign(size_t(w) * h * 4, 0);
            }
            uint32_t index = 0;
            for (auto& [id, m] : next)
            {
                uint32_t x = (index % cols) * stride, y = (index / cols) * stride;
                ++index;
                m.first = uint32_t(packed->vertices.size());
                m.count = uint32_t(m.data->vertices.size());
                packed->vertices.insert(packed->vertices.end(), m.data->vertices.begin(), m.data->vertices.end());
                m.uv = {float(tile - 1) / w, float(tile - 1) / h, float(x + 2.5f) / w, float(y + 2.5f) / h};
                for (size_t k = 0; k < 5; ++k)
                {
                    const auto& src = m.data->images[k];
                    auto&       dst = packed->images[k];
                    for (uint32_t py = 0; py < stride; ++py)
                        for (uint32_t px = 0; px < stride; ++px)
                        {
                            float u = std::clamp((float(px) - 2) / float(std::max(1u, tile - 1)), 0.f, 1.f),
                                  v = std::clamp((float(py) - 2) / float(std::max(1u, tile - 1)), 0.f, 1.f);
                            auto sx = std::min(src.width - 1, uint32_t(std::round(u * (src.width - 1)))),
                                 sy = std::min(src.height - 1, uint32_t(std::round(v * (src.height - 1))));
                            std::memcpy(
                              dst.pixels.data() + (size_t(y + py) * w + x + px) * 4,
                              src.pixels.data() + (size_t(sy) * src.width + sx) * 4,
                              4);
                        }
                }
            }
            model  = std::move(packed);
            meshes = std::move(next);
            key    = std::move(nextKey);
        }
        std::map<std::string, bool> spaces;
        for (const auto& a : assets)
            spaces[a.id] = a.sourceSpace;
        auto effective = input;
        for (auto& i : effective)
            if (spaces.at(i.assetId))
            {
                const auto& data  = *meshes.at(i.assetId).data;
                float       scale = 0;
                for (int k = 0; k < 3; k++)
                    scale = std::max(scale, (data.sourceMaximum[k] - data.sourceMinimum[k]) * .5f);
                glm::mat4 restore(scale);
                restore[3] = glm::vec4(0, 0, 0, 1);
                for (int k = 0; k < 3; k++)
                    restore[3][k] = (data.sourceMinimum[k] + data.sourceMaximum[k]) * .5f;
                auto world = glm::make_mat4(i.world.data()) * restore;
                std::memcpy(i.world.data(), glm::value_ptr(world), 64);
            }
        std::vector<InstanceData> gpu;
        std::vector<SceneDraw>    groups;
        for (const auto& [id, m] : meshes)
        {
            uint32_t start = uint32_t(gpu.size());
            for (const auto& i : effective)
                if (i.assetId == id)
                {
                    InstanceData v{};
                    std::copy(i.world.begin(), i.world.end(), v.world);
                    auto      world       = glm::make_mat4(i.world.data());
                    auto      linear      = glm::mat3(world);
                    float     determinant = glm::determinant(linear);
                    glm::mat4 normal(1);
                    if (std::abs(determinant) > 1e-12f)
                        normal = glm::mat4(glm::transpose(glm::inverse(linear)));
                    else
                        normal = glm::mat4(0);
                    std::memcpy(v.normal, glm::value_ptr(normal), 64);
                    std::copy(m.data->baseColor.begin(), m.data->baseColor.end(), v.baseColor);
                    std::copy(m.data->material.begin(), m.data->material.end(), v.material);
                    std::copy(m.data->emissive.begin(), m.data->emissive.end(), v.emissive);
                    std::copy(m.uv.begin(), m.uv.end(), v.uv);
                    for (float value : v.world)
                        if (!std::isfinite(value))
                            throw std::invalid_argument("Instance transform overflows float32");
                    for (float value : v.normal)
                        if (!std::isfinite(value))
                            throw std::invalid_argument("Instance normal transform overflows float32");
                    gpu.push_back(v);
                }
            groups.push_back({m.first, m.count, start, uint32_t(gpu.size()) - start});
        }
        pickInstances = std::move(effective);
        instances    = std::move(gpu);
        draws        = std::move(groups);
        sourceSpaces = std::move(spaces);
        active       = true;
    }

    std::optional<SceneHit> SceneData::Pick(const Ray& ray) const
    {
        glm::dvec3 origin(ray.origin[0], ray.origin[1], ray.origin[2]),
          direction(ray.direction[0], ray.direction[1], ray.direction[2]);
        for (float v : ray.origin)
            if (!std::isfinite(v))
                throw std::invalid_argument("Ray origin must be finite");
        for (float v : ray.direction)
            if (!std::isfinite(v))
                throw std::invalid_argument("Ray direction must be finite");
        const double length = std::hypot(direction.x, direction.y, direction.z);
        if (length == 0)
            throw std::invalid_argument("Ray direction must be nonzero");
        for (const auto* list : {&ray.includeIds, &ray.excludeIds})
            for (const auto& id : *list)
                if (id.empty())
                    throw std::invalid_argument("Ray filter IDs must be nonempty");
        direction /= length;
        std::optional<SceneHit> best;
        if (!active)
            return best;
        const std::set<std::string> included(ray.includeIds.begin(), ray.includeIds.end()),
          excluded(ray.excludeIds.begin(), ray.excludeIds.end());
        for (const auto& instance : pickInstances)
        {
            if (excluded.count(instance.id) || (!included.empty() && !included.count(instance.id)))
                continue;
            const auto&      m = instance.world;
            const glm::dvec3 a(m[0], m[1], m[2]), b(m[4], m[5], m[6]), c(m[8], m[9], m[10]),
              offset(m[12], m[13], m[14]);
            const auto   x = glm::cross(b, c), y = glm::cross(c, a), z = glm::cross(a, b);
            const double determinant = glm::dot(a, x);
            if (determinant == 0)
                continue;
            auto inverse = [&](glm::dvec3 v) -> PickVector {
                return {glm::dot(x, v) / determinant, glm::dot(y, v) / determinant, glm::dot(z, v) / determinant};
            };
            const auto hit = meshes.at(instance.assetId)
                               .picking->Pick(
                                 inverse(origin - offset),
                                 inverse(direction),
                                 best ? best->distance : std::numeric_limits<double>::infinity());
            if (!hit || (best && hit->distance >= best->distance))
                continue;
            auto         n       = x * hit->normal[0] + y * hit->normal[1] + z * hit->normal[2];
            const double nLength = std::hypot(n.x, n.y, n.z);
            if (nLength == 0 || !std::isfinite(nLength))
                continue;
            n /= nLength;
            const auto           p = origin + direction * hit->distance;
            std::array<float, 3> position{float(p.x), float(p.y), float(p.z)};
            if (!std::all_of(position.begin(), position.end(), [](float f) { return std::isfinite(f); }))
                continue;
            best = SceneHit{instance.id, hit->distance, position, {float(n.x), float(n.y), float(n.z)}};
        }
        return best;
    }

    std::optional<Bounds> SceneData::AssetBounds(const std::string& id) const
    {
        auto found = meshes.find(id);
        if (found == meshes.end())
            return {};
        const auto& d = *found->second.data;
        Bounds      out{d.sourceMinimum, d.sourceMaximum};
        if (!sourceSpaces.at(id))
        {
            float scale = 0;
            for (int k = 0; k < 3; k++)
                scale = std::max(scale, (out.maximum[k] - out.minimum[k]) * .5f);
            for (int k = 0; k < 3; k++)
            {
                float center   = (out.maximum[k] + out.minimum[k]) * .5f;
                out.minimum[k] = (out.minimum[k] - center) / scale;
                out.maximum[k] = (out.maximum[k] - center) / scale;
            }
        }
        return out;
    }
} // namespace render::detail
