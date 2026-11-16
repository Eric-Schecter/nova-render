#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "ModelData.hpp"
#include "AnimationData.hpp"
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <tiny_gltf.h>

namespace render::detail
{
    namespace
    {
        // The initial loader deliberately supports opaque, single-material GLBs.
        const unsigned char* element(
          const tinygltf::Model& model, int accessorIndex, size_t index, int type, int component)
        {
            const auto& a = model.accessors.at(accessorIndex);
            if (
              a.sparse.isSparse || a.type != type || a.componentType != component || index >= a.count
              || a.bufferView < 0)
                throw std::runtime_error("Unsupported glTF accessor");
            const auto&  view   = model.bufferViews.at(a.bufferView);
            const auto&  data   = model.buffers.at(view.buffer).data;
            const int    stride = a.ByteStride(view);
            const size_t size   = tinygltf::GetComponentSizeInBytes(component) * tinygltf::GetNumComponentsInType(type);
            if (
              stride <= 0 || view.byteOffset > data.size() || view.byteLength > data.size() - view.byteOffset
              || a.byteOffset > view.byteLength
              || index > (view.byteLength - a.byteOffset) / static_cast<size_t>(stride))
                throw std::runtime_error("Invalid glTF buffer bounds");
            const size_t offset = a.byteOffset + index * static_cast<size_t>(stride);
            if (offset > view.byteLength || size > view.byteLength - offset)
                throw std::runtime_error("Truncated glTF accessor");
            return data.data() + view.byteOffset + offset;
        }
        glm::vec3 vec3(const tinygltf::Model& m, int a, size_t i)
        {
            glm::vec3 v;
            std::memcpy(&v, element(m, a, i, TINYGLTF_TYPE_VEC3, TINYGLTF_COMPONENT_TYPE_FLOAT), 12);
            return v;
        }
        glm::vec2 vec2(const tinygltf::Model& m, int a, size_t i)
        {
            glm::vec2 v;
            std::memcpy(&v, element(m, a, i, TINYGLTF_TYPE_VEC2, TINYGLTF_COMPONENT_TYPE_FLOAT), 8);
            return v;
        }
    } // namespace
    std::shared_ptr<const ModelData> LoadGLB(const std::filesystem::path& path)
    {
        tinygltf::Model    gltf;
        tinygltf::TinyGLTF loader;
        std::string        errors, warnings;
        if (!loader.LoadBinaryFromFile(&gltf, &errors, &warnings, path.string()))
            throw std::runtime_error("Load GLB: " + path.string() + ": " + errors);
        for (const auto& list : {gltf.extensionsRequired, gltf.extensionsUsed})
            for (const auto& extension : list)
                if (extension != "KHR_materials_transmission" && extension != "KHR_materials_ior")
                    throw std::runtime_error("Unsupported glTF extension: " + extension);
        auto dielectric = [](const tinygltf::Material& m) {
            float      transmission = 0, ior = 1.5f;
            const auto t = m.extensions.find("KHR_materials_transmission");
            if (t != m.extensions.end())
            {
                if (t->second.Has("transmissionTexture"))
                    throw std::runtime_error("Transmission textures are not yet supported");
                if (t->second.Has("transmissionFactor") && !t->second.Get("transmissionFactor").IsNumber())
                    throw std::runtime_error("Transmission factor must be numeric");
                if (t->second.Has("transmissionFactor"))
                    transmission = float(t->second.Get("transmissionFactor").GetNumberAsDouble());
            }
            const auto i = m.extensions.find("KHR_materials_ior");
            if (i != m.extensions.end() && i->second.Has("ior") && !i->second.Get("ior").IsNumber())
                throw std::runtime_error("IOR must be numeric");
            if (i != m.extensions.end() && i->second.Has("ior"))
                ior = float(i->second.Get("ior").GetNumberAsDouble());
            if (
              !std::isfinite(transmission) || transmission < 0 || transmission > 1 || !std::isfinite(ior) || ior < 1.01f
              || ior > 3)
                throw std::runtime_error("Dielectric requires transmission 0..1 and IOR 1.01..3");
            return std::array<float, 2>{transmission, ior};
        };
        auto              animation     = ReadAnimationData(gltf);
        auto              result        = std::make_shared<ModelData>();
        int               materialIndex = -1;
        glm::vec3         minimum(std::numeric_limits<float>::max()), maximum(-std::numeric_limits<float>::max());
        std::vector<bool> active(gltf.nodes.size(), false);
        std::function<void(int, glm::mat4)> visit = [&](int index, glm::mat4 parent) {
            if (index < 0 || static_cast<size_t>(index) >= gltf.nodes.size() || active[index])
                throw std::runtime_error("Invalid glTF scene graph");
            active[index]    = true;
            const auto& node = gltf.nodes.at(index);
            glm::mat4   local(1);
            if (node.matrix.size() == 16)
            {
                for (int i = 0; i < 16; ++i)
                    glm::value_ptr(local)[i] = static_cast<float>(node.matrix[i]);
            }
            else
            {
                if (node.translation.size() == 3)
                    local =
                      glm::translate(local, glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
                if (node.rotation.size() == 4)
                    local *= glm::mat4_cast(
                      glm::quat(
                        static_cast<float>(node.rotation[3]),
                        static_cast<float>(node.rotation[0]),
                        static_cast<float>(node.rotation[1]),
                        static_cast<float>(node.rotation[2])));
                if (node.scale.size() == 3)
                    local = glm::scale(local, glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
            }
            const auto matrix  = parent * local;
            const auto normals = glm::transpose(glm::inverse(glm::mat3(matrix)));
            if (node.mesh >= 0)
                for (const auto& p : gltf.meshes.at(node.mesh).primitives)
                {
                    if (p.mode != TINYGLTF_MODE_TRIANGLES || !p.targets.empty())
                        throw std::runtime_error("Only triangle primitives without morph targets are supported");
                    if (materialIndex == -1)
                        materialIndex = p.material;
                    if (p.material < 0)
                        throw std::runtime_error("Missing material");
                    auto common = gltf.materials.at(materialIndex), candidate = gltf.materials.at(p.material);
                    common.extensions.clear();
                    candidate.extensions.clear();
                    common.name.clear();
                    candidate.name.clear();
                    if (!(common == candidate))
                        throw std::runtime_error("Materials must share core PBR factors and texture atlas");
                    const auto glass     = dielectric(gltf.materials.at(p.material));
                    const int  position  = p.attributes.at("POSITION"),
                              uv         = p.attributes.count("TEXCOORD_0") ? p.attributes.at("TEXCOORD_0") : -1;
                    const auto& material = gltf.materials.at(p.material);
                    if(uv<0 && (material.pbrMetallicRoughness.baseColorTexture.index>=0||material.pbrMetallicRoughness.metallicRoughnessTexture.index>=0||material.normalTexture.index>=0||material.occlusionTexture.index>=0||material.emissiveTexture.index>=0))
                        throw std::runtime_error("Textured material requires TEXCOORD_0");
                    const int  normal = p.attributes.count("NORMAL") ? p.attributes.at("NORMAL") : -1;
                    const auto count =
                      p.indices >= 0 ? gltf.accessors.at(p.indices).count : gltf.accessors.at(position).count;
                    if (count % 3 || result->vertices.size() + count > 10000000)
                        throw std::runtime_error("Invalid or oversized triangle mesh");
                    auto vertexIndex = [&](size_t i) {
                        if (p.indices < 0)
                            return uint32_t(i);
                        const auto c = gltf.accessors.at(p.indices).componentType;
                        if (c != 5121 && c != 5123 && c != 5125)
                            throw std::runtime_error("Invalid index format");
                        uint32_t v = 0;
                        std::memcpy(
                          &v,
                          element(gltf, p.indices, i, TINYGLTF_TYPE_SCALAR, c),
                          tinygltf::GetComponentSizeInBytes(c));
                        return v;
                    };
                    for (size_t i = 0; i < count; ++i)
                    {
                        const auto v = vertexIndex(i);
                        glm::vec3  localNormal;
                        if (normal >= 0)
                            localNormal = vec3(gltf, normal, v);
                        else
                        {
                            const auto first = i - i % 3;
                            const auto a     = vec3(gltf, position, vertexIndex(first)),
                                       b     = vec3(gltf, position, vertexIndex(first + 1)),
                                       c     = vec3(gltf, position, vertexIndex(first + 2));
                            localNormal      = glm::normalize(glm::cross(b - a, c - a));
                        }
                        const glm::vec3 pos = glm::vec3(matrix * glm::vec4(vec3(gltf, position, v), 1));
                        const glm::vec3 n   = glm::normalize(normals * localNormal);
                        const glm::vec2 t   = uv >= 0 ? vec2(gltf, uv, v) : glm::vec2(0);
                        for (int k = 0; k < 3; ++k)
                            if (!std::isfinite(pos[k]) || !std::isfinite(n[k]))
                                throw std::runtime_error("Non-finite model data");
                        if (!std::isfinite(t.x) || !std::isfinite(t.y))
                            throw std::runtime_error("Non-finite texture coordinates");
                        minimum = glm::min(minimum, pos);
                        maximum = glm::max(maximum, pos);
                        if (animation)
                            AppendSkinVertex(*animation, gltf, node.skin, index, p, v, matrix, localNormal);
                        result->vertices.push_back(
                          {{pos.x, pos.y, pos.z, 1},
                           {n.x, n.y, n.z, glass[0]},
                           {t.x, t.y, normal < 0 ? 1.0f : 0.0f, glass[1]}});
                    }
                }
            for (int child : node.children)
                visit(child, matrix);
            active[index] = false;
        };
        const int scene = gltf.defaultScene >= 0 ? gltf.defaultScene : 0;
        for (int root : gltf.scenes.at(scene).nodes)
            visit(root, glm::mat4(1));
        if (result->vertices.empty())
            throw std::runtime_error("GLB contains no triangles");
        for (int k = 0; k < 3; k++)
        {
            result->sourceMinimum[k] = minimum[k];
            result->sourceMaximum[k] = maximum[k];
        }
        const auto  center = (minimum + maximum) * 0.5f;
        const auto  extent = maximum - minimum;
        const float size   = std::max({extent.x, extent.y, extent.z});
        if (!(size > 0))
            throw std::runtime_error("Degenerate GLB bounds");
        for (auto& v : result->vertices)
            for (int k = 0; k < 3; ++k)
                v.position[k] = (v.position[k] - center[k]) * 2.0f / size;
        if (animation)
        {
            animation->center = center;
            animation->scale  = 2.0f / size;
            result->animation = animation;
        }
        const auto& mat = gltf.materials.at(materialIndex);
        const auto& pbr = mat.pbrMetallicRoughness;
        if (mat.alphaMode != "OPAQUE")
            throw std::runtime_error("Only opaque materials are supported");
        for (int k = 0; k < 4; ++k)
            result->baseColor[k] = static_cast<float>(pbr.baseColorFactor[k]);
        result->material = {
          static_cast<float>(pbr.metallicFactor),
          static_cast<float>(pbr.roughnessFactor),
          static_cast<float>(mat.normalTexture.scale),
          static_cast<float>(mat.occlusionTexture.strength)};
        for (int k = 0; k < 3; ++k)
            result->emissive[k] = static_cast<float>(mat.emissiveFactor[k]);
        if (
          pbr.baseColorTexture.texCoord || mat.normalTexture.texCoord || pbr.metallicRoughnessTexture.texCoord
          || mat.occlusionTexture.texCoord || mat.emissiveTexture.texCoord)
            throw std::runtime_error("Only TEXCOORD_0 is supported");
        const int textures[] = {
          pbr.baseColorTexture.index,
          mat.normalTexture.index,
          pbr.metallicRoughnessTexture.index,
          mat.occlusionTexture.index,
          mat.emissiveTexture.index};
        const std::array<std::array<unsigned char, 4>, 5> fallback = {
          {{255, 255, 255, 255}, {128, 128, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 255}, {0, 0, 0, 255}}};
        for (int i = 0; i < 5; ++i)
        {
            auto& dst = result->images[i];
            dst.pixels.assign(fallback[i].begin(), fallback[i].end());
            if (textures[i] < 0)
                continue;
            const auto& image = gltf.images.at(gltf.textures.at(textures[i]).source);
            if (
              image.width <= 0 || image.height <= 0 || image.width > 8192 || image.height > 8192 || image.bits != 8
              || image.component != 4 || image.image.size() != static_cast<size_t>(image.width) * image.height * 4)
                throw std::runtime_error("Expected RGBA8 glTF image");
            dst.width  = image.width;
            dst.height = image.height;
            dst.pixels = image.image;
        }
        std::cout << "Model: " << path.filename().string() << "; triangles: " << result->vertices.size() / 3 << '\n';
        return result;
    }
    void ValidateCamera(const OrbitCamera& c)
    {
        for (float v : c.target)
            if (!std::isfinite(v))
                throw std::invalid_argument("Camera target must be finite");
        if (
          !std::isfinite(c.yaw) || !std::isfinite(c.pitch) || !std::isfinite(c.distance) || !std::isfinite(c.exposure)
          || std::abs(c.pitch) > 85 || c.distance < 1.5f || c.distance > 100 || c.exposure <= 0 || c.exposure > 16)
            throw std::invalid_argument("Invalid orbit camera (pitch +/-85, distance 1.5..100, exposure 0..16)");
    }
    void ValidateDirectionalLight(const DirectionalLight& light)
    {
        for (float v : light.direction)
            if (!std::isfinite(v))
                throw std::invalid_argument("Light direction must be finite");
        if (std::hypot(double(light.direction[0]), double(light.direction[1]), double(light.direction[2])) < 1e-6)
            throw std::invalid_argument("Light direction must be nonzero (length >= 1e-6)");
        for (float v : light.color)
            if (!std::isfinite(v) || v < 0 || v > 1)
                throw std::invalid_argument("Light color must be linear RGB in [0,1]");
        if (!std::isfinite(light.intensity) || light.intensity < 0 || light.intensity > 100)
            throw std::invalid_argument("Light intensity must be in [0,100]");
    }
    std::shared_ptr<const ModelData> EmptyModelData()
    {
        static const auto empty = [] {
            auto                                              data   = std::make_shared<ModelData>();
            const std::array<std::array<unsigned char, 4>, 5> colors = {
              {{255, 255, 255, 255}, {128, 128, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 255}, {0, 0, 0, 255}}};
            for (size_t i = 0; i < 5; ++i)
                data->images[i].pixels.assign(colors[i].begin(), colors[i].end());
            return data;
        }();
        return empty;
    }
    SceneUniforms MakeSceneUniforms(
      const ModelData&           data,
      const OrbitCamera&         camera,
      float                      aspect,
      LightingMode               lighting,
      const DirectionalLight&    light,
      const EnvironmentData*     environment,
      const EnvironmentSettings& settings)
    {
        const float     yaw = glm::radians(camera.yaw), pitch = glm::radians(camera.pitch);
        const glm::vec3 target(camera.target[0], camera.target[1], camera.target[2]);
        const glm::vec3 eye = target
                              + glm::vec3(
                                camera.distance * std::cos(pitch) * std::sin(yaw),
                                camera.distance * std::sin(pitch),
                                camera.distance * std::cos(pitch) * std::cos(yaw));
        const auto vp = glm::perspectiveRH_ZO(glm::radians(45.0f), aspect, 0.01f, 200.0f)
                        * glm::lookAtRH(eye, target, glm::vec3(0, 1, 0));
        SceneUniforms result{};
        std::memcpy(result.viewProjection, glm::value_ptr(vp), 64);
        result.eyeExposure[0] = eye.x;
        result.eyeExposure[1] = eye.y;
        result.eyeExposure[2] = eye.z;
        result.eyeExposure[3] = camera.exposure;
        std::copy(data.baseColor.begin(), data.baseColor.end(), result.baseColor);
        std::copy(data.material.begin(), data.material.end(), result.material);
        std::copy(data.emissive.begin(), data.emissive.end(), result.emissive);
        const double length =
          std::hypot(double(light.direction[0]), double(light.direction[1]), double(light.direction[2]));
        for (int k = 0; k < 3; ++k)
        {
            result.lightDirection[k]      = static_cast<float>(light.direction[k] / length);
            result.lightColorIntensity[k] = light.color[k];
        }
        result.lightColorIntensity[3] = light.intensity;
        const glm::vec3 forward       = glm::normalize(target - eye),
                        right         = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0))),
                        up            = glm::cross(right, forward);
        for (int k = 0; k < 3; ++k)
        {
            result.cameraRight[k]   = right[k];
            result.cameraUp[k]      = up[k];
            result.cameraForward[k] = forward[k];
        }
        result.cameraRight[3]       = std::tan(glm::radians(22.5f)) * aspect;
        result.cameraUp[3]          = std::tan(glm::radians(22.5f));
        result.cameraForward[3]     = environment ? 1.0f : 0.0f;
        result.environmentParams[0] = std::sin(glm::radians(settings.rotation));
        result.environmentParams[1] = std::cos(glm::radians(settings.rotation));
        result.environmentParams[2] = settings.intensity;
        result.environmentParams[3] = environment ? float(environment->mips.size() - 1) : 0;
        result.skyParams[0]         = settings.backgroundIntensity;
        result.skyParams[1]         = settings.showBackground ? 1.0f : 0.0f;
        result.skyParams[3]         = data.animation ? 1.0f : 0.0f;
        if (environment)
            for (int k = 0; k < 9; ++k)
                std::copy(environment->irradiance[k].begin(), environment->irradiance[k].end(), result.irradiance[k]);
        result.irradiance[0][3] = 0; // Reserved raw-HDR flag; environment padding must not enable it.
        result.emissive[3]      = lighting == LightingMode::PBR ? 1.0f : 0.0f;
        return result;
    }
} // namespace render::detail
