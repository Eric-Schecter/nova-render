#include "AnimationData.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <set>
#include <stdexcept>
namespace render::detail
{
    namespace
    {
#ifdef NOVA_HAS_GLTF
        glm::vec4 Read(const tinygltf::Model& m, int index, size_t row, int components, bool integer = false)
        {
            const auto& a    = m.accessors.at(index);
            const auto& v    = m.bufferViews.at(a.bufferView);
            const auto& b    = m.buffers.at(v.buffer).data;
            int         size = tinygltf::GetComponentSizeInBytes(a.componentType);
            if (
              a.sparse.isSparse || size <= 0 || row >= a.count
              || tinygltf::GetNumComponentsInType(a.type) != components)
                throw std::runtime_error("Unsupported animation accessor");
            const size_t stride = v.byteStride ? v.byteStride : size * components;
            const size_t start  = a.byteOffset + row * stride;
            if (
              stride < size_t(size * components) || start + size * components > v.byteLength
              || v.byteOffset + start + size * components > b.size())
                throw std::runtime_error("Animation accessor out of bounds");
            glm::vec4 result(0);
            for (int k = 0; k < components; ++k)
            {
                const auto* p = b.data() + v.byteOffset + start + k * size;
                if (a.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && !integer)
                {
                    float value;
                    std::memcpy(&value, p, 4);
                    result[k] = value;
                }
                else if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                    result[k] = *p;
                else if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                {
                    uint16_t value;
                    std::memcpy(&value, p, 2);
                    result[k] = value;
                }
                else
                    throw std::runtime_error("Unsupported animation component type");
                if (a.normalized)
                {
                    if (integer || a.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
                        throw std::runtime_error("Normalized joint indices");
                    result[k] /= a.componentType == 5121 ? 255.f : 65535.f;
                }
                if (!std::isfinite(result[k]))
                    throw std::runtime_error("Non-finite animation data");
            }
            return result;
        }
#endif
        glm::quat Quaternion(glm::vec4 v)
        {
            if (glm::length(v) < 1e-8f)
                throw std::runtime_error("Zero animation quaternion");
            return glm::normalize(glm::quat(v.w, v.x, v.y, v.z));
        }
    } // namespace
#ifdef NOVA_HAS_GLTF
    std::shared_ptr<AnimationData> ReadAnimationData(const tinygltf::Model& m)
    {
        if (m.skins.empty() && m.animations.empty())
            return {};
        auto out = std::make_shared<AnimationData>();
        out->nodes.resize(m.nodes.size());
        for (size_t i = 0; i < m.nodes.size(); ++i)
        {
            const auto& n    = m.nodes[i];
            auto&       node = out->nodes[i];
            node.name = n.name;
            if (n.mesh >= 0)
            {
                const auto& mesh = m.meshes.at(n.mesh);
                const auto count = mesh.primitives.empty() ? 0 : mesh.primitives.front().targets.size();
                const auto& weights = n.weights.empty() ? mesh.weights : n.weights;
                if (!weights.empty() && weights.size() != count)
                    throw std::runtime_error("Invalid morph weights");
                node.morphWeights.resize(count);
                std::copy(weights.begin(), weights.end(), node.morphWeights.begin());
            }
            if (n.translation.size() == 3)
                node.translation = {n.translation[0], n.translation[1], n.translation[2]};
            if (n.scale.size() == 3)
                node.scale = {n.scale[0], n.scale[1], n.scale[2]};
            if (n.rotation.size() == 4)
                node.rotation = Quaternion({n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3]});
            if (n.matrix.size() == 16)
            {
                node.hasMatrix = true;
                for (int k = 0; k < 16; ++k)
                    glm::value_ptr(node.matrix)[k] = float(n.matrix[k]);
            }
            for (int child : n.children)
            {
                if (child < 0 || size_t(child) >= m.nodes.size() || out->nodes[child].parent != -1)
                    throw std::runtime_error("Invalid animation hierarchy");
                out->nodes[child].parent = int(i);
            }
        }
        for (const auto& n : out->nodes)
        {
            int    p     = n.parent;
            size_t depth = 0;
            while (p >= 0)
            {
                if (++depth > out->nodes.size())
                    throw std::runtime_error("Animation hierarchy cycle");
                p = out->nodes[p].parent;
            }
        }
        for (const auto& skin : m.skins)
        {
            AnimationSkin s;
            s.joints = skin.joints;
            if (s.joints.empty())
                throw std::runtime_error("Empty skin");
            if (skin.inverseBindMatrices >= 0)
            {
                const auto& a = m.accessors.at(skin.inverseBindMatrices);
                if (
                  a.type != TINYGLTF_TYPE_MAT4 || a.componentType != 5126 || a.count != s.joints.size()
                  || a.sparse.isSparse)
                    throw std::runtime_error("Invalid inverse bind matrices");
                const auto& v = m.bufferViews.at(a.bufferView);
                const auto& b = m.buffers.at(v.buffer).data;
                for (size_t i = 0; i < a.count; ++i)
                {
                    const size_t offset = v.byteOffset + a.byteOffset + i * (v.byteStride ? v.byteStride : 64);
                    if (offset + 64 > b.size() || offset + 64 > v.byteOffset + v.byteLength)
                        throw std::runtime_error("Inverse bind out of bounds");
                    glm::mat4 matrix;
                    std::memcpy(glm::value_ptr(matrix), b.data() + offset, 64);
                    s.inverseBind.push_back(matrix);
                }
            }
            else
                s.inverseBind.assign(s.joints.size(), glm::mat4(1));
            for (int joint : s.joints)
                if (joint < 0 || size_t(joint) >= m.nodes.size())
                    throw std::runtime_error("Invalid skin joint");
            out->skins.push_back(std::move(s));
        }
        for (const auto& animation : m.animations)
        {
            AnimationClip clip{
              animation.name.empty() ? "Animation " + std::to_string(out->clips.size() + 1) : animation.name, 0};
            std::vector<AnimationTrack>           tracks;
            std::set<std::pair<int, std::string>> targets;
            for (const auto& channel : animation.channels)
            {
                const auto&    sampler = animation.samplers.at(channel.sampler);
                AnimationTrack track;
                track.node          = channel.target_node;
                track.path          = channel.target_path;
                track.interpolation = sampler.interpolation.empty() ? "LINEAR" : sampler.interpolation;
                if (
                  track.node < 0 || size_t(track.node) >= m.nodes.size() || (out->nodes[track.node].hasMatrix && track.path != "weights")
                  || !targets.insert({track.node, track.path}).second)
                    throw std::runtime_error("Invalid animation target");
                if (track.path != "translation" && track.path != "rotation" && track.path != "scale" && track.path != "weights")
                    throw std::runtime_error("Unsupported animation channel");
                const bool cubic = track.interpolation == "CUBICSPLINE";
                if (!cubic && track.interpolation != "LINEAR" && track.interpolation != "STEP")
                    throw std::runtime_error("Unsupported animation interpolation");
                const auto& input  = m.accessors.at(sampler.input);
                const auto& output = m.accessors.at(sampler.output);
                const bool weights = track.path == "weights";
                const size_t targetsCount = weights ? out->nodes[track.node].morphWeights.size() : 1;
                const size_t rows = (cubic ? 3 : 1) * targetsCount;
                if (input.componentType != 5126 || output.componentType != 5126 || !input.count
                    || !rows || output.count / rows != input.count || output.count % rows)
                    throw std::runtime_error("Invalid animation sampler");
                for (size_t i = 0; i < input.count; ++i)
                {
                    const float t = Read(m, sampler.input, i, 1).x;
                    if (t < 0 || (!track.times.empty() && t <= track.times.back()))
                        throw std::runtime_error("Invalid animation times");
                    track.times.push_back(t);
                }
                for (size_t target = 0; target < targetsCount; ++target)
                {
                    auto channelTrack = track;
                    channelTrack.morphTarget = target;
                    const int components = weights ? 1 : track.path == "rotation" ? 4 : 3;
                    for (size_t i = 0; i < input.count; ++i)
                    {
                        const size_t key = (cubic ? i * 3 + 1 : i) * targetsCount + target;
                        const auto value = Read(m, sampler.output, key, components);
                        if (cubic)
                        {
                            channelTrack.inTangents.push_back(Read(m, sampler.output, key - targetsCount, components));
                            channelTrack.outTangents.push_back(Read(m, sampler.output, key + targetsCount, components));
                        }
                        if (track.path == "rotation")
                            Quaternion(value);
                        channelTrack.values.push_back(value);
                    }
                    tracks.push_back(std::move(channelTrack));
                }
                clip.duration = std::max(clip.duration, track.times.back());
            }
            if (tracks.empty() || clip.duration <= 0)
                throw std::runtime_error("Empty animation clip");
            out->clips.push_back(clip);
            out->tracks.push_back(std::move(tracks));
        }
        return out;
    }
    void AppendSkinVertex(
      AnimationData&             out,
      const tinygltf::Model&     m,
      int                        skin,
      int                        nodeIndex,
      const tinygltf::Primitive& p,
      uint32_t                   index,
      const glm::mat4&           matrix,
      const glm::vec3&           localPosition,
      const glm::vec3&           localNormal)
    {
        SkinVertex v;
        v.position = localPosition;
        v.normal   = localNormal;
        v.matrix   = matrix;
        v.skin     = skin;
        v.node     = nodeIndex;
        if (skin >= 0)
        {
            const auto& s = out.skins.at(skin);
            v.joints      = glm::uvec4(Read(m, p.attributes.at("JOINTS_0"), index, 4, true));
            v.weights     = Read(m, p.attributes.at("WEIGHTS_0"), index, 4);
            float total   = 0;
            for (int k = 0; k < 4; ++k)
            {
                if (v.joints[k] >= s.joints.size() || v.weights[k] < 0)
                    throw std::runtime_error("Invalid skin influence");
                total += v.weights[k];
            }
            if (total < 1e-8f)
                throw std::runtime_error("Zero skin weights");
            v.weights /= total;
        }
        out.vertices.push_back(v);
    }
#endif
    void AnimationPlayer::Load(std::shared_ptr<const ModelData> data)
    {
        source = std::move(data);
        posed.reset();
        state = {};
        if (source->animation)
        {
            posed = std::make_shared<ModelData>(*source);
            Apply();
        }
    }
    std::vector<AnimationClip> AnimationPlayer::Clips() const
    {
        return source && source->animation ? source->animation->clips : std::vector<AnimationClip>{};
    }
    void AnimationPlayer::Select(uint32_t clip)
    {
        const auto clips = Clips();
        if (clip >= clips.size())
            throw std::invalid_argument("Invalid animation clip");
        state.clip = clip;
        state.time = 0;
        Apply();
    }
    void AnimationPlayer::Seek(float time)
    {
        const auto clips = Clips();
        if (!std::isfinite(time) || time < 0 || clips.empty() || time > clips.at(state.clip).duration)
            throw std::invalid_argument("Invalid animation time");
        state.time = time;
        Apply();
    }
    void AnimationPlayer::Update(float delta)
    {
        if (!std::isfinite(delta) || delta < 0)
            throw std::invalid_argument("Invalid animation delta");
        const auto clips = Clips();
        if (!state.playing || clips.empty() || !delta)
            return;
        const float duration = clips.at(state.clip).duration;
        float       time     = state.time + delta * state.speed;
        if (!std::isfinite(time))
            throw std::invalid_argument("Animation time overflow");
        if (state.loop)
            time = std::fmod(time, duration);
        else if (time >= duration)
        {
            time          = duration;
            state.playing = false;
        }
        state.time = time;
        Apply();
    }
    AnimationState ValidateAnimationState(const std::vector<AnimationClip>& clips, const AnimationState& state)
    {
        if (
          state.clip >= clips.size() || !std::isfinite(state.time) || state.time < 0
          || state.time > clips[state.clip].duration || !std::isfinite(state.speed) || state.speed < .1f
          || state.speed > 4)
            throw std::invalid_argument("Invalid animation state");
        return state;
    }
    AnimationState AdvanceAnimation(const std::vector<AnimationClip>& clips, AnimationState state, float delta)
    {
        if (!std::isfinite(delta) || delta < 0)
            throw std::invalid_argument("Invalid animation delta");
        if (!state.playing || delta == 0)
            return state;
        double time = double(state.time) + double(delta) * state.speed;
        if (!std::isfinite(time) || time > std::numeric_limits<float>::max())
            throw std::invalid_argument("Animation time overflow");
        float duration = clips.at(state.clip).duration;
        if (state.loop)
            time = std::fmod(time, duration);
        else if (time >= duration)
        {
            time          = duration;
            state.playing = false;
        }
        state.time = float(time);
        return state;
    }
    std::vector<JointMatrix> EvaluateAnimation(const AnimationData& a, const AnimationState& state)
    {
        auto nodes = a.nodes;
        if (!a.clips.empty())
            for (const auto& track : a.tracks.at(state.clip))
            {
                size_t r = std::upper_bound(track.times.begin(), track.times.end(), state.time) - track.times.begin();
                r        = std::min(r, track.times.size() - 1);
                size_t l = r && state.time < track.times[r] ? r - 1 : r;
                float  t = l == r || track.interpolation == "STEP"
                             ? 0
                             : (state.time - track.times[l]) / (track.times[r] - track.times[l]);
                auto   value = glm::mix(track.values[l], track.values[r], t);
                const bool cubic = track.interpolation == "CUBICSPLINE";
                if (cubic && l != r)
                {
                    const float dt = track.times[r] - track.times[l];
                    const float t2 = t * t, t3 = t2 * t;
                    value = (2 * t3 - 3 * t2 + 1) * track.values[l]
                            + (t3 - 2 * t2 + t) * dt * track.outTangents[l]
                            + (-2 * t3 + 3 * t2) * track.values[r]
                            + (t3 - t2) * dt * track.inTangents[r];
                }
                auto& n = nodes[track.node];
                if (track.path == "rotation")
                    n.rotation = cubic ? Quaternion(value)
                                       : glm::slerp(Quaternion(track.values[l]), Quaternion(track.values[r]), t);
                else if (track.path == "translation")
                    n.translation = value;
                else if (track.path == "weights")
                    n.morphWeights[track.morphTarget] = value.x;
                else
                    n.scale = value;
            }
        std::vector<glm::mat4>        world(nodes.size());
        std::vector<bool>             ready(nodes.size());
        std::function<glm::mat4(int)> global = [&](int i) {
            if (ready[i])
                return world[i];
            const auto& n     = nodes[i];
            auto        local = n.hasMatrix ? n.matrix
                                            : glm::translate(glm::mat4(1), n.translation) * glm::mat4_cast(n.rotation)
                                         * glm::scale(glm::mat4(1), n.scale);
            world[i]          = n.parent >= 0 ? global(n.parent) * local : local;
            ready[i]          = true;
            return world[i];
        };
        size_t count = a.nodes.size();
        for (const auto& skin : a.skins)
            count += skin.joints.size();
        glm::mat4 normalize = glm::scale(glm::mat4(1), glm::vec3(a.scale));
        normalize[3]        = glm::vec4(-a.center * a.scale, 1);
        std::vector<JointMatrix> matrices;
        matrices.reserve(count);
        auto append = [&](const glm::mat4& matrix) {
            JointMatrix value;
            std::memcpy(value.data(), glm::value_ptr(matrix), 64);
            for (float x : value)
                if (!std::isfinite(x))
                    throw std::runtime_error("Non-finite joint matrix");
            matrices.push_back(value);
        };
        for (const auto& skin : a.skins)
            for (size_t i = 0; i < skin.joints.size(); ++i)
                append(normalize * global(skin.joints[i]) * skin.inverseBind[i]);
        for (size_t i = 0; i < nodes.size(); ++i)
            append(normalize * global(int(i)));
        for (const auto& node : nodes)
            for (size_t i = 0; i < node.morphWeights.size(); i += 16)
            {
                JointMatrix weights{};
                std::copy_n(node.morphWeights.begin() + i, std::min(size_t(16), node.morphWeights.size() - i), weights.begin());
                matrices.push_back(weights);
            }
        return matrices;
    }
    void AnimationPlayer::Apply()
    {
        if (!posed)
            return;
        const auto&           a = *source->animation;
        std::vector<uint32_t> offsets;
        size_t                count = 0;
        for (const auto& skin : a.skins)
        {
            offsets.push_back(uint32_t(count));
            count += skin.joints.size();
        }
        const auto nodeOffset = uint32_t(count);
        count += a.nodes.size();
        if (posed->skinInputs.empty())
        {
            std::vector<uint32_t> morphOffsets;
            for (const auto& node : a.nodes)
            {
                morphOffsets.push_back(uint32_t(count));
                count += (node.morphWeights.size() + 15) / 16;
            }
            posed->skinInputs.resize(a.vertices.size());
            size_t vertex = 0;
            for (const auto& v : a.vertices)
            {
                SkinInput input;
                input.position = {v.position.x, v.position.y, v.position.z, 1};
                input.normal   = {v.normal.x, v.normal.y, v.normal.z, 0};
                for (int k = 0; k < 4; ++k)
                {
                    input.joints[k]  = v.skin >= 0 ? offsets[v.skin] + v.joints[k] : nodeOffset + uint32_t(v.node);
                    input.weights[k] = v.skin >= 0 ? v.weights[k] : (k == 0 ? 1.f : 0.f);
                }
                if (!v.morphDeltas.empty())
                {
                    if (posed->skinInputs.size() + v.morphDeltas.size() > (1u << 24))
                        throw std::runtime_error("Morph vertex data exceeds the storage index range");
                    input.normal[3] = float(posed->skinInputs.size() + 1);
                    for (size_t i = 0; i < v.morphDeltas.size(); ++i)
                    {
                        SkinInput delta;
                        const auto& d = v.morphDeltas[i];
                        delta.position = {d[0].x, d[0].y, d[0].z, 0};
                        delta.normal = {d[1].x, d[1].y, d[1].z, 0};
                        delta.joints = {morphOffsets[v.node] + uint32_t(i / 16), uint32_t(i % 16),
                                        uint32_t(v.morphDeltas.size()), 0};
                        posed->skinInputs.push_back(delta);
                    }
                }
                posed->skinInputs[vertex++] = input;
            }
        }
        posed->jointMatrices = EvaluateAnimation(a, state);
        ++posed->revision;
    }
} // namespace render::detail
