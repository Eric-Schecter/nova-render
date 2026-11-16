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
                auto picking = old != meshes.end() && old->second.data == data ? old->second.picking
                                                                               : std::make_shared<PickMesh>(data);
                next[id]     = {data, 0, 0, {}, std::move(picking)};
            }
            auto packed = std::make_shared<ModelData>(*EmptyModelData());
            for (auto& [id, m] : next)
            {
                m.first     = uint32_t(packed->vertices.size());
                m.count     = uint32_t(m.data->vertices.size());
                auto offset = packed->materials.size();
                packed->vertices.insert(packed->vertices.end(), m.data->vertices.begin(), m.data->vertices.end());
                for (size_t i = m.first; i < packed->vertices.size(); i++)
                    packed->vertices[i].position[3] += float(offset);
                packed->materials.insert(packed->materials.end(), m.data->materials.begin(), m.data->materials.end());
            }
            PackMaterials(*packed);
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
                    v.uv[0] = float(input.size());
                    v.uv[3] = -1;
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
        assetsSnapshot = assets;
        inputSnapshot  = input;
    }

    std::vector<Material> SceneData::GetMaterials(const std::string& id) const
    {
        auto m = meshes.find(id);
        if (!active || m == meshes.end())
            throw std::invalid_argument("Unknown scene asset");
        return m->second.data->materials;
    }
    void SceneData::SetMaterial(const std::string& id, uint32_t slot, const Material& material)
    {
        auto next  = *this;
        auto found = next.meshes.find(id);
        if (!active || found == next.meshes.end() || slot >= found->second.data->materials.size())
            throw std::invalid_argument("Unknown scene material slot");
        auto data = std::make_shared<ModelData>(*found->second.data);
        ValidateMaterial(material);
        data->materials[slot] = material;
        PackMaterials(*data);
        found->second.data = data;
        next.model.reset();
        next.Build(assetsSnapshot, inputSnapshot);
        *this = std::move(next);
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
