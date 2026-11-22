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
        if (!active)
            players.clear();
        std::map<std::string, std::filesystem::path> paths;
        std::map<std::string, std::optional<uint32_t>> variants;
        for (const auto& a : assets)
            if (a.id.empty() || a.path.empty() || !paths.emplace(a.id, a.path).second)
                throw std::invalid_argument("Scene assets require unique nonempty IDs and paths");
        for (const auto& a : assets) variants.emplace(a.id, a.materialVariant);
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
        std::vector<AssetKey> nextKey;
        for (const auto& id : referenced)
            nextKey.emplace_back(id, paths.at(id), variants.at(id));
        if (!model || nextKey != key)
        {
            std::map<std::string, Mesh> next;
            for (const auto& entry : nextKey)
            {
                const auto& [id, path, variant] = entry;
                std::shared_ptr<const ModelData> data;
                auto                             old  = meshes.find(id);
                auto                             prev = std::find(key.begin(), key.end(), entry);
                if (old != meshes.end() && prev != key.end())
                    data = old->second.data;
                else
                    data = LoadGLB(path, variant);
                if (data->animation && data->skinInputs.empty())
                {
                    AnimationPlayer prepare;
                    prepare.Load(data);
                    data = prepare.Model();
                }
                auto picking = old != meshes.end() && old->second.data == data ? old->second.picking
                                                                               : std::make_shared<PickMesh>(data);
                next[id]     = {data, 0, 0, 0, 0, {}, std::move(picking)};
            }
            auto packed = std::make_shared<ModelData>(*EmptyModelData());
            for (auto& [id, m] : next)
            {
                m.first     = uint32_t(packed->vertices.size());
                m.count     = uint32_t(m.data->vertices.size());
                auto offset = packed->materials.size();
                packed->hasTransmission |= m.data->hasTransmission;
                packed->vertices.insert(packed->vertices.end(), m.data->vertices.begin(), m.data->vertices.end());
                for (size_t i = m.first; i < packed->vertices.size(); i++)
                    packed->vertices[i].position[3] += float(offset);
                packed->materials.insert(packed->materials.end(), m.data->materials.begin(), m.data->materials.end());
            }
            bool animated = false;
            for (const auto& [id, m] : next)
                animated = animated || bool(m.data->animation);
            if (animated)
            {
                packed->skinInputs.resize(packed->vertices.size());
                for (const auto& [id, m] : next)
                    if (m.data->animation)
                    {
                        const auto& inputs = m.data->skinInputs;
                        const auto vertices = m.data->vertices.size();
                        const auto extra = packed->skinInputs.size();
                        if (extra + inputs.size() - vertices > (1u << 24))
                            throw std::runtime_error("Scene morph data exceeds the storage index range");
                        for (size_t i = 0; i < vertices; ++i)
                        {
                            auto input = inputs[i];
                            if (input.normal[3] > 0)
                                input.normal[3] += float(extra - vertices);
                            packed->skinInputs[m.first + i] = input;
                        }
                        packed->skinInputs.insert(packed->skinInputs.end(), inputs.begin() + vertices, inputs.end());
                    }
            }
            PackMaterials(*packed);
            std::vector<InstanceData> shadow;
            for (auto& [id, m] : next)
            {
                m.shadowFirst    = uint32_t(shadow.size());
                m.shadowNodes    = m.picking->ShadowNodeCount();
                size_t shadowRow = 0;
                const auto rows = m.picking->ShadowRecords();
                if (rows.size() > shadowBVHByteLimit / 192 - shadow.size())
                    throw std::runtime_error("Scene shadow BVHs exceed 384 MiB");
                for (const auto& row : rows)
                {
                    InstanceData record;
                    std::memcpy(&record, row.data(), 192);
                    if (shadowRow++ >= m.shadowNodes)
                        record.emissive[3] += packed->vertices[m.first].position[3] - m.data->vertices[0].position[3];
                    shadow.push_back(record);
                }
            }
            shadowRecords = std::move(shadow);
            model         = std::move(packed);
            meshes        = std::move(next);
            key           = std::move(nextKey);
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
        std::map<std::string, Player> nextPlayers;
        size_t                        paletteCount = 0;
        for (const auto& i : input)
        {
            const auto& mesh = meshes.at(i.assetId);
            if (!mesh.data->animation)
                continue;
            Player player;
            player.assetId       = i.assetId;
            player.path          = paths.at(i.assetId);
            player.state.playing = false;
            auto previous        = players.find(i.id);
            if (
              previous != players.end() && previous->second.assetId == i.assetId
              && previous->second.path == player.path)
                player = previous->second;
            player.paletteOffset = uint32_t(paletteCount);
            player.palette       = EvaluateAnimation(*mesh.data->animation, player.state);
            paletteCount += player.palette.size();
            if (paletteCount * 64ull > 64ull * 1024 * 1024)
                throw std::runtime_error("Scene animation palettes exceed 64 MiB");
            nextPlayers.emplace(i.id, std::move(player));
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
                    if (m.data->animation)
                        v.normal[12] = float(nextPlayers.at(i.id).paletteOffset + 1);
                    v.uv[0] = float(input.size());
                    v.uv[1] = float(m.shadowFirst);
                    v.uv[2] = i.castShadow && std::abs(determinant) > 1e-12f ? 1.f : 0.f;
                    v.uv[3] = float(m.shadowNodes);
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
        std::vector<JointMatrix> nextPalette;
        for (const auto& i : input)
        {
            auto p = nextPlayers.find(i.id);
            if (p != nextPlayers.end())
                nextPalette.insert(nextPalette.end(), p->second.palette.begin(), p->second.palette.end());
        }
        if (model->jointMatrices != nextPalette)
        {
            if (!model.unique())
                model = std::make_shared<ModelData>(*model);
            auto packed           = std::const_pointer_cast<ModelData>(model);
            packed->jointMatrices = std::move(nextPalette);
            ++packed->revision;
        }
        players        = std::move(nextPlayers);
        inputSnapshot  = input;
        instances      = std::move(gpu);
        draws          = std::move(groups);
        sourceSpaces   = std::move(spaces);
        active         = true;
        assetsSnapshot = assets;
        inputSnapshot  = input;
    }

    std::shared_ptr<const ModelData> SceneData::RayModel()
    {
        if (
          rayModel && raySource == model && rayInstances.size() == instances.size()
          && (instances.empty() || std::memcmp(rayInstances.data(), instances.data(), instances.size() * sizeof(InstanceData)) == 0))
            return rayModel;
        auto baked = std::make_shared<ModelData>(*model);
        baked->vertices.clear();
        size_t count = 0;
        for (const auto& d : draws)
        {
            if (d.vertexCount && d.instanceCount > (128ull * 1024 * 1024 / sizeof(ModelVertex) - count) / d.vertexCount)
                throw std::runtime_error("Ray scene geometry exceeds 128 MiB");
            count += size_t(d.vertexCount) * d.instanceCount;
        }
        baked->vertices.reserve(count);
        for (const auto& d : draws)
            for (uint32_t i = 0; i < d.instanceCount; i++)
            {
                const auto& instance = instances[d.firstInstance + i];
                auto        world    = glm::make_mat4(instance.world);
                auto        normal   = glm::mat3(glm::make_mat4(instance.normal));
                if (std::abs(glm::determinant(glm::mat3(world))) < 1e-12f)
                    continue;
                for (uint32_t j = 0; j < d.vertexCount; j++)
                {
                    auto v = model->vertices[d.firstVertex + j];
                    auto p = world * glm::vec4(v.position[0], v.position[1], v.position[2], 1);
                    auto n = normal * glm::vec3(v.normal[0], v.normal[1], v.normal[2]);
                    for (int k = 0; k < 3; k++)
                    {
                        if (!std::isfinite(p[k]) || !std::isfinite(n[k]))
                            throw std::runtime_error("Ray scene transform overflows float32");
                        v.position[k] = p[k];
                    }
                    float length = glm::length(n);
                    if (length > 0)
                        n /= length;
                    for (int k = 0; k < 3; k++)
                        v.normal[k] = n[k];
                    // Path shaders reserve uv.z < 0 for noncasting instances; primary/transport rays still see them.
                    v.uv[2] = instance.uv[2] > .5f ? 0.f : -1.f;
                    baked->vertices.push_back(v);
                }
            }
        if (baked->vertices.empty())
        {
            ModelVertex dummy{};
            dummy.position[3] = 1;
            dummy.normal[1]   = 1;
            baked->vertices.assign(3, dummy);
        }
        rayModel     = baked;
        raySource    = model;
        rayInstances = instances;
        return rayModel;
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
            auto picking = meshes.at(instance.assetId).picking;
            auto player  = players.find(instance.id);
            if (player != players.end())
            {
                auto geometry = PoseGeometry(instance.id);
                if (!player->second.queryPick)
                    player->second.queryPick = std::make_shared<PickMesh>(geometry);
                picking = player->second.queryPick;
            }
            const auto hit = picking->Pick(
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
        if (found->second.data->animation)
        {
            Bounds result;
            result.minimum.fill(std::numeric_limits<float>::infinity());
            result.maximum.fill(-std::numeric_limits<float>::infinity());
            const auto& data = *found->second.data;
            float       half = 0;
            for (int k = 0; k < 3; ++k)
                half = std::max(half, (data.sourceMaximum[k] - data.sourceMinimum[k]) * .5f);
            for (const auto& [instance, p] : players)
                if (p.assetId == id)
                    for (const auto& v : PoseGeometry(instance)->vertices)
                        for (int k = 0; k < 3; ++k)
                        {
                            float value = v.position[k];
                            if (sourceSpaces.at(id))
                                value = value * half + (data.sourceMinimum[k] + data.sourceMaximum[k]) * .5f;
                            result.minimum[k] = std::min(result.minimum[k], value);
                            result.maximum[k] = std::max(result.maximum[k], value);
                        }
            return result;
        }
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

namespace render::detail
{
    std::vector<AnimationClip> SceneData::Animations(const std::string& id) const
    {
        auto m = meshes.find(id);
        if (!active || m == meshes.end())
            throw std::invalid_argument("Unknown scene asset");
        return m->second.data->animation ? m->second.data->animation->clips : std::vector<AnimationClip>{};
    }
    AnimationState SceneData::Animation(const std::string& id) const
    {
        auto p = players.find(id);
        if (!active || p == players.end())
            throw std::invalid_argument("Instance has no animation");
        return p->second.state;
    }
    void SceneData::AppendAnimatedShadows(std::vector<InstanceData>& records, size_t shadowBase) const
    {
        size_t index = 0;
        for (const auto& [assetId, mesh] : meshes)
            for (const auto& instance : pickInstances)
            {
                if (instance.assetId != assetId)
                    continue;
                auto& record = records[index++];
                if (!mesh.data->animation || !instance.castShadow || record.uv[2] == 0)
                    continue;
                const auto& player = players.at(instance.id);
                if (!player.queryPick)
                    player.queryPick = std::make_shared<PickMesh>(PoseGeometry(instance.id));
                const auto&  rows      = player.queryPick->ShadowRecords();
                const size_t nodeCount = player.queryPick->ShadowNodeCount();
                if ((records.size() - shadowBase + rows.size()) * 192ull > shadowBVHByteLimit)
                    throw std::runtime_error("Scene shadow BVHs exceed 384 MiB");
                record.uv[1] = float(records.size() - shadowBase);
                record.uv[3] = float(nodeCount);
                const float materialOffset =
                  model->vertices[mesh.first].position[3] - mesh.data->vertices[0].position[3];
                for (size_t row = 0; row < rows.size(); ++row)
                {
                    InstanceData packed;
                    std::memcpy(&packed, rows[row].data(), sizeof(packed));
                    if (row >= nodeCount)
                        packed.emissive[3] += materialOffset;
                    records.push_back(packed);
                }
            }
    }
    void SceneData::CommitPalettes()
    {
        auto packed = std::const_pointer_cast<ModelData>(model);
        packed->jointMatrices.clear();
        for (const auto& input : inputSnapshot)
        {
            auto p = players.find(input.id);
            if (p != players.end())
                packed->jointMatrices.insert(
                  packed->jointMatrices.end(), p->second.palette.begin(), p->second.palette.end());
        }
        ++packed->revision;
    }
    void SceneData::SetAnimation(const std::string& id, const AnimationState& state)
    {
        auto found = players.find(id);
        if (!active || found == players.end())
            throw std::invalid_argument("Instance has no animation");
        const auto& data      = *meshes.at(found->second.assetId).data->animation;
        auto        next      = ValidateAnimationState(data.clips, state);
        auto        palette   = EvaluateAnimation(data, next);
        found->second.state   = next;
        found->second.palette = std::move(palette);
        found->second.query.reset();
        found->second.queryPick.reset();
        CommitPalettes();
    }
    void SceneData::UpdateAnimations(float delta)
    {
        if (!std::isfinite(delta) || delta < 0)
            throw std::invalid_argument("Invalid animation delta");
        if (!active || players.empty())
            return;
        auto next = players;
        for (auto& [id, p] : next)
        {
            const auto& data = *meshes.at(p.assetId).data->animation;
            p.state          = AdvanceAnimation(data.clips, p.state, delta);
            auto palette     = EvaluateAnimation(data, p.state);
            if (palette != p.palette)
            {
                p.query.reset();
                p.queryPick.reset();
            }
            p.palette = std::move(palette);
        }
        players = std::move(next);
        CommitPalettes();
    }
} // namespace render::detail

namespace render::detail
{
    std::shared_ptr<const ModelData> SceneData::PoseGeometry(const std::string& id) const
    {
        const auto& p = players.at(id);
        if (p.query)
            return p.query;
        const auto& source = *meshes.at(p.assetId).data;
        return p.query     = PoseAnimationGeometry(source, p.palette);
    }
    std::shared_ptr<const ModelData> PoseAnimationGeometry(
      const ModelData& source, const std::vector<JointMatrix>& palette)
    {
        auto out      = std::make_shared<ModelData>();
        out->vertices = source.vertices;
        for (size_t i = 0; i < source.vertices.size(); ++i)
        {
            const auto& input = source.skinInputs[i];
            glm::mat4   matrix(0);
            for (int j = 0; j < 4; ++j)
                matrix += input.weights[j] * glm::make_mat4(palette.at(input.joints[j]).data());
            auto local = glm::make_vec4(input.position.data());
            if (input.normal[3] > 0)
            {
                const auto first = size_t(input.normal[3]) - 1;
                const auto count = source.skinInputs.at(first).joints[2];
                for (size_t j = 0; j < count; ++j)
                {
                    const auto& delta = source.skinInputs.at(first + j);
                    local += glm::make_vec4(delta.position.data()) * palette.at(delta.joints[0])[delta.joints[1]];
                }
            }
            auto position = matrix * local;
            for (int k = 0; k < 3; ++k)
                out->vertices[i].position[k] = position[k];
        }
        return out;
    }
    std::optional<Bounds> SceneData::InstanceBounds(const std::string& id) const
    {
        auto input =
          std::find_if(pickInstances.begin(), pickInstances.end(), [&](const auto& i) { return i.id == id; });
        if (!active || input == pickInstances.end())
            return {};
        auto   geometry = players.count(id) ? PoseGeometry(id) : meshes.at(input->assetId).data;
        Bounds result;
        result.minimum.fill(std::numeric_limits<float>::infinity());
        result.maximum.fill(-std::numeric_limits<float>::infinity());
        auto world = glm::make_mat4(input->world.data());
        for (const auto& v : geometry->vertices)
        {
            auto position = world * glm::vec4(v.position[0], v.position[1], v.position[2], 1);
            for (int k = 0; k < 3; ++k)
            {
                result.minimum[k] = std::min(result.minimum[k], position[k]);
                result.maximum[k] = std::max(result.maximum[k], position[k]);
            }
        }
        return result;
    }
} // namespace render::detail
