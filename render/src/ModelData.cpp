#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "ModelData.hpp"
#include "AnimationData.hpp"
#include "CameraData.hpp"
#include <glm/gtc/quaternion.hpp>
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>
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
        const unsigned char* bufferElement(
          const tinygltf::Model& model, int viewIndex, size_t offset, size_t index, size_t stride, size_t size)
        {
            const auto& view = model.bufferViews.at(viewIndex);
            const auto& data = model.buffers.at(view.buffer).data;
            if (stride < size || view.byteOffset > data.size() || view.byteLength > data.size() - view.byteOffset
                || offset > view.byteLength || size > view.byteLength - offset
                || index > (view.byteLength - offset - size) / stride)
                throw std::runtime_error("Invalid glTF buffer bounds");
            return data.data() + view.byteOffset + offset + index * stride;
        }
        const unsigned char* element(
          const tinygltf::Model& model, int accessorIndex, size_t index, int type, int component)
        {
            const auto& a = model.accessors.at(accessorIndex);
            if (a.type != type || a.componentType != component || index >= a.count)
                throw std::runtime_error("Unsupported glTF accessor");
            const size_t size = tinygltf::GetComponentSizeInBytes(component) * tinygltf::GetNumComponentsInType(type);
            if (a.sparse.isSparse)
            {
                const auto& sparse = a.sparse;
                const int componentType = sparse.indices.componentType;
                if (sparse.count <= 0 || size_t(sparse.count) > a.count
                    || (componentType != 5121 && componentType != 5123 && componentType != 5125))
                    throw std::runtime_error("Invalid sparse glTF accessor");
                const size_t width = tinygltf::GetComponentSizeInBytes(componentType);
                size_t first = 0, last = size_t(sparse.count);
                while (first < last)
                {
                    const auto row = first + (last - first) / 2;
                    const auto* bytes = bufferElement(
                      model, sparse.indices.bufferView, sparse.indices.byteOffset, row, width, width);
                    uint32_t vertex = 0;
                    std::memcpy(&vertex, bytes, width);
                    if (vertex >= a.count)
                        throw std::runtime_error("Invalid sparse glTF index");
                    if (vertex == index)
                        return bufferElement(model, sparse.values.bufferView, sparse.values.byteOffset, row, size, size);
                    if (vertex < index)
                        first = row + 1;
                    else
                        last = row;
                }
            }
            if (a.bufferView < 0)
            {
                static constexpr unsigned char zero[16]{};
                if (!a.sparse.isSparse || size > sizeof(zero))
                    throw std::runtime_error("Missing glTF buffer view");
                return zero;
            }
            const int stride = a.ByteStride(model.bufferViews.at(a.bufferView));
            if (stride <= 0)
                throw std::runtime_error("Invalid glTF buffer stride");
            return bufferElement(model, a.bufferView, a.byteOffset, index, size_t(stride), size);
        }
        glm::vec3 vec3(const tinygltf::Model& m, int a, size_t i)
        {
            glm::vec3 v;
            std::memcpy(&v, element(m, a, i, TINYGLTF_TYPE_VEC3, TINYGLTF_COMPONENT_TYPE_FLOAT), 12);
            return v;
        }
        struct TextureCoordinates
        {
            int set = 0;
            std::array<float, 2> offset{0, 0}, scale{1, 1};
            float angle = 0;
            bool operator!=(const TextureCoordinates& rhs) const
            {
                return set != rhs.set || offset != rhs.offset || scale != rhs.scale || angle != rhs.angle;
            }
        };
        TextureCoordinates textureCoordinates(int source, const tinygltf::Value& extension)
        {
            TextureCoordinates coordinates;
            coordinates.set = source;
            if (extension.Has("texCoord"))
            {
                if (!extension.Get("texCoord").IsInt())
                    throw std::runtime_error("Invalid texture coordinate override");
                coordinates.set = extension.Get("texCoord").GetNumberAsInt();
            }
            if (coordinates.set < 0)
                throw std::runtime_error("Invalid texture coordinate set");
            auto read = [](const tinygltf::Value& value) {
                if (!value.IsNumber() || !std::isfinite(float(value.GetNumberAsDouble())))
                    throw std::runtime_error("Invalid texture transform");
                return float(value.GetNumberAsDouble());
            };
            for (auto [key, output] : {std::pair{"offset", &coordinates.offset}, std::pair{"scale", &coordinates.scale}})
                if (extension.Has(key))
                {
                    const auto& values = extension.Get(key);
                    if (!values.IsArray() || values.ArrayLen() != 2)
                        throw std::runtime_error("Invalid texture transform");
                    *output = {read(values.Get(0)), read(values.Get(1))};
                }
            if (extension.Has("rotation"))
                coordinates.angle = read(extension.Get("rotation"));
            return coordinates;
        }
        const tinygltf::Value& materialExtension(const tinygltf::Material& material, const char* name)
        {
            static const tinygltf::Value empty;
            const auto it = material.extensions.find(name);
            return it == material.extensions.end() ? empty : it->second;
        }
        int volumeTextureIndex(const tinygltf::Value& extension, const char* name)
        {
            if (!extension.Has(name))
                return -1;
            const auto& info = extension.Get(name);
            if (!info.Has("index") || !info.Get("index").IsInt() || info.Get("index").GetNumberAsInt() < 0)
                throw std::runtime_error("Invalid glTF volume texture");
            return info.Get("index").GetNumberAsInt();
        }
        void expandMeshInstances(tinygltf::Model& model)
        {
            constexpr char extension[] = "EXT_mesh_gpu_instancing";
            for (size_t i = 0, originalCount = model.nodes.size(); i < originalCount; ++i)
            {
                const auto node = model.nodes[i];
                const auto found = node.extensions.find(extension);
                if (found == node.extensions.end())
                    continue;
                const auto& attributes = found->second.Get("attributes");
                if (node.mesh < 0 || node.skin >= 0 || !attributes.IsObject()
                    || attributes.Keys().empty())
                    throw std::runtime_error("Invalid glTF mesh instances");
                size_t count = 0;
                for (const auto& key : attributes.Keys())
                {
                    const auto& value = attributes.Get(key);
                    if (!value.IsInt())
                        throw std::runtime_error("Invalid instance accessor");
                    const auto rows = model.accessors.at(value.GetNumberAsInt()).count;
                    if (!rows || (count && count != rows))
                        throw std::runtime_error("Mismatched glTF instance accessors");
                    count = rows;
                }
                const auto& mesh = model.meshes.at(node.mesh);
                size_t vertexCount = 0;
                for (const auto& primitive : mesh.primitives)
                    vertexCount += model.accessors.at(primitive.attributes.at("POSITION")).count;
                if (!vertexCount || count > 10000000 / vertexCount)
                    throw std::runtime_error("Oversized glTF mesh instances");
                for (size_t row = 0; row < count; ++row)
                {
                    tinygltf::Node instance;
                    instance.mesh = node.mesh;
                    instance.weights = node.weights;
                    for (const auto* key : {"TRANSLATION", "ROTATION", "SCALE"})
                    {
                        if (!attributes.Has(key))
                            continue;
                        const int index = attributes.Get(key).GetNumberAsInt();
                        if (std::string_view(key) == "ROTATION")
                        {
                            const auto& accessor = model.accessors.at(index);
                            const int type = accessor.componentType;
                            if (!((type == 5126 && !accessor.normalized)
                               || ((type == 5120 || type == 5122) && accessor.normalized)))
                                throw std::runtime_error("Invalid instance rotation encoding");
                            const auto* data = element(model, index, row, TINYGLTF_TYPE_VEC4, type);
                            glm::vec4 rotation{};
                            for (int k = 0; k < 4; ++k)
                                if (type == 5126)
                                    std::memcpy(&rotation[k], data + k * 4, 4);
                                else if (type == 5120)
                                    rotation[k] = std::max(-1.f, float(int8_t(data[k])) / 127.f);
                                else
                                {
                                    int16_t component;
                                    std::memcpy(&component, data + k * 2, 2);
                                    rotation[k] = std::max(-1.f, float(component) / 32767.f);
                                }
                            const float length = glm::length(rotation);
                            if (!std::isfinite(length) || length < 1e-8f)
                                throw std::runtime_error("Invalid instance rotation");
                            rotation /= length;
                            instance.rotation.assign(&rotation[0], &rotation[0] + 4);
                        }
                        else
                        {
                            const auto v = vec3(model, index, row);
                            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
                                throw std::runtime_error("Invalid glTF instance vector");
                            auto& result = std::string_view(key) == "TRANSLATION" ? instance.translation : instance.scale;
                            result = {v.x, v.y, v.z};
                        }
                    }
                    model.nodes[i].children.push_back(int(model.nodes.size()));
                    model.nodes.push_back(std::move(instance));
                }
                model.nodes[i].mesh = -1;
                model.nodes[i].extensions.erase(extension);
            }
        }
        glm::vec2 vec2(const tinygltf::Model& m, int a, size_t i)
        {
            const auto& accessor  = m.accessors.at(a);
            const int   component = accessor.componentType;
            glm::vec2 v;
            if (component == TINYGLTF_COMPONENT_TYPE_FLOAT && !accessor.normalized)
                std::memcpy(&v, element(m, a, i, TINYGLTF_TYPE_VEC2, component), sizeof(v));
            else if (component == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE && accessor.normalized)
            {
                const auto* values = element(m, a, i, TINYGLTF_TYPE_VEC2, component);
                v                  = glm::vec2(values[0], values[1]) / 255.f;
            }
            else if (component == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT && accessor.normalized)
            {
                uint16_t values[2];
                std::memcpy(values, element(m, a, i, TINYGLTF_TYPE_VEC2, component), sizeof(values));
                v = glm::vec2(values[0], values[1]) / 65535.f;
            }
            else
                throw std::runtime_error("Unsupported glTF texture coordinate encoding");
            return v;
        }
        glm::vec4 vertexColor(const tinygltf::Model& m, int index, size_t vertex)
        {
            const auto& a = m.accessors.at(index);
            if (a.type != TINYGLTF_TYPE_VEC3 && a.type != TINYGLTF_TYPE_VEC4)
                throw std::runtime_error("COLOR_0 requires VEC3 or VEC4");
            const int count = a.type == TINYGLTF_TYPE_VEC3 ? 3 : 4;
            const int component = a.componentType;
            if (!(component == 5126 && !a.normalized) && !((component == 5121 || component == 5123) && a.normalized))
                throw std::runtime_error("Invalid COLOR_0 encoding");
            const auto* bytes = element(m, index, vertex, a.type, component);
            glm::vec4 color(1);
            if (component == 5126)
                std::memcpy(&color, bytes, count * sizeof(float));
            else if (component == 5121)
                for (int k = 0; k < count; ++k)
                    color[k] = bytes[k] / 255.f;
            else
                for (int k = 0; k < count; ++k)
                {
                    uint16_t value;
                    std::memcpy(&value, bytes + k * sizeof(value), sizeof(value));
                    color[k] = value / 65535.f;
                }
            for (int k = 0; k < count; ++k)
                if (!std::isfinite(color[k]))
                    throw std::runtime_error("Invalid COLOR_0 value");
            return glm::clamp(color, 0.f, 1.f);
        }
        constexpr std::array coatTextureKeys{"clearcoatTexture", "clearcoatRoughnessTexture", "clearcoatNormalTexture"};
        struct CoatTexture
        {
            int      index = -1;
            uint32_t uv    = 0;
            float    scale = 1;
        };
        CoatTexture readCoatTexture(const tinygltf::Value& extension, size_t k)
        {
            CoatTexture result;
            const auto* key = coatTextureKeys[k];
            if (!extension.Has(key))
                return result;
            const auto& info = extension.Get(key);
            if (!info.IsObject() || !info.Get("index").IsInt() || info.Get("index").GetNumberAsInt() < 0)
                throw std::runtime_error("Invalid clearcoat texture index");
            result.index = info.Get("index").GetNumberAsInt();
            if (info.Has("texCoord"))
            {
                const auto& uv = info.Get("texCoord");
                if (!uv.IsInt() || uv.GetNumberAsInt() < 0 || uv.GetNumberAsInt() > 1)
                    throw std::runtime_error("Clearcoat coordinate set must be 0 or 1");
                result.uv = uint32_t(uv.GetNumberAsInt());
            }
            if (k == 2 && info.Has("scale"))
            {
                if (!info.Get("scale").IsNumber())
                    throw std::runtime_error("Invalid clearcoat normal scale");
                result.scale = float(info.Get("scale").GetNumberAsDouble());
            }
            return result;
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
                if (
                  extension != "KHR_materials_transmission" && extension != "KHR_materials_volume" && extension != "KHR_materials_ior"
                  && extension != "KHR_materials_specular" && extension != "KHR_materials_clearcoat"
                  && extension != "KHR_materials_emissive_strength" && extension != "KHR_xmp_json_ld"
                  && extension != "KHR_materials_unlit" && extension != "KHR_texture_transform" && extension != "KHR_xmp" && extension != "EXT_mesh_gpu_instancing")
                    throw std::runtime_error("Unsupported glTF extension: " + extension);
        auto dielectric = [](const tinygltf::Material& m) {
            float      transmission = 0, ior = 1.5f;
            const auto t = m.extensions.find("KHR_materials_transmission");
            if (t != m.extensions.end())
            {
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
        expandMeshInstances(gltf);
        auto              animation = ReadAnimationData(gltf);
        auto              result    = std::make_shared<ModelData>();
        glm::vec3         minimum(std::numeric_limits<float>::max()), maximum(-std::numeric_limits<float>::max());
        std::vector<bool> active(gltf.nodes.size(), false);
        const auto defaultMaterial = gltf.materials.size();
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
                    if (p.mode < TINYGLTF_MODE_POINTS || p.mode > TINYGLTF_MODE_TRIANGLE_FAN)
                        throw std::runtime_error("Unsupported glTF primitive mode");
                    if (p.material < -1 || (p.material >= 0 && size_t(p.material) >= defaultMaterial))
                        throw std::runtime_error("Invalid glTF material index");
                    const auto materialIndex = p.material == -1 ? defaultMaterial : size_t(p.material);
                    if (p.material == -1 && gltf.materials.size() == defaultMaterial)
                        gltf.materials.emplace_back();
                    const auto glass = dielectric(gltf.materials.at(materialIndex));
                    result->hasTransmission |= glass[0] > 0;
                    const int  position  = p.attributes.at("POSITION"),
                              uv         = p.attributes.count("TEXCOORD_0") ? p.attributes.at("TEXCOORD_0") : -1;
                    const int   uv1      = p.attributes.count("TEXCOORD_1") ? p.attributes.at("TEXCOORD_1") : -1;
                    const auto colorIndex = p.attributes.find("COLOR_0");
                    const auto& material = gltf.materials.at(materialIndex);
                    const auto& pbr      = material.pbrMetallicRoughness;
                    const auto& ao       = material.occlusionTexture;
                    std::array<std::optional<TextureCoordinates>, 2> coordinates;
                    auto assign = [&](int stream, TextureCoordinates value) {
                        if (stream < 0 || stream > 1 || (coordinates[stream] && *coordinates[stream] != value))
                            throw std::runtime_error("Material requires independent texture coordinate streams");
                        coordinates[stream] = value;
                    };
                    auto addTexture = [&](const auto& texture, int stream) {
                        if (texture.index < 0)
                            return;
                        const auto extension = texture.extensions.find("KHR_texture_transform");
                        assign(stream, textureCoordinates(texture.texCoord,
                          extension == texture.extensions.end() ? tinygltf::Value{} : extension->second));
                    };
                    addTexture(pbr.baseColorTexture, 0);
                    addTexture(pbr.metallicRoughnessTexture, 0);
                    addTexture(material.normalTexture, 0);
                    const int emissionStream = material.emissiveTexture.texCoord == 1
                      && (pbr.baseColorTexture.index >= 0 || pbr.metallicRoughnessTexture.index >= 0
                          || material.normalTexture.index >= 0) ? 1 : 0;
                    addTexture(material.emissiveTexture, emissionStream);
                    addTexture(ao, ao.texCoord);
                    for (const auto& pair : {std::pair{"KHR_materials_transmission", "transmissionTexture"},
                                            std::pair{"KHR_materials_volume", "thicknessTexture"}})
                    {
                        const auto& ext = materialExtension(material, pair.first);
                        if (volumeTextureIndex(ext, pair.second) < 0)
                            continue;
                        const auto& texture = ext.Get(pair.second);
                        const auto texCoord = texture.Has("texCoord") ? texture.Get("texCoord").GetNumberAsInt() : 0;
                        const auto& extensions = texture.Get("extensions");
                        assign(0, textureCoordinates(texCoord, extensions.Has("KHR_texture_transform")
                            ? extensions.Get("KHR_texture_transform") : tinygltf::Value{}));
                    }
                    if (const auto coat = material.extensions.find("KHR_materials_clearcoat");
                        coat != material.extensions.end())
                        for (size_t k = 0; k < coatTextureKeys.size(); ++k)
                        {
                            const auto info = readCoatTexture(coat->second, k);
                            if (info.index >= 0)
                            {
                                const auto& extensions = coat->second.Get(coatTextureKeys[k]).Get("extensions");
                                assign(info.uv, textureCoordinates(info.uv, extensions.Has("KHR_texture_transform")
                                  ? extensions.Get("KHR_texture_transform") : tinygltf::Value{}));
                            }
                        }
                    std::array<std::pair<glm::vec2, glm::mat2>, 2> transforms;
                    std::array<int, 2> uvAccessors{uv, uv1};
                    for (int stream = 0; stream < 2; ++stream)
                    {
                        transforms[stream] = {glm::vec2(0), glm::mat2(1)};
                        if (!coordinates[stream])
                            continue;
                        const auto& c = *coordinates[stream];
                        const auto accessor = p.attributes.find("TEXCOORD_" + std::to_string(c.set));
                        if (accessor == p.attributes.end())
                            throw std::runtime_error("Textured material is missing its texture coordinate set");
                        uvAccessors[stream] = accessor->second;
                        const auto sin = std::sin(c.angle), cos = std::cos(c.angle);
                        transforms[stream] = {glm::vec2(c.offset[0], c.offset[1]),
                          glm::mat2(cos * c.scale[0], -sin * c.scale[0], sin * c.scale[1], cos * c.scale[1])};
                    }
                    const int  normal = p.attributes.count("NORMAL") ? p.attributes.at("NORMAL") : -1;
                    const auto& weights = node.weights.empty() ? gltf.meshes.at(node.mesh).weights : node.weights;
                    if (!weights.empty() && weights.size() != p.targets.size())
                        throw std::runtime_error("Invalid morph weight count");
                    auto morph = [&](const char* semantic, glm::vec3 base, size_t vertex) {
                        for (size_t target = 0; target < weights.size(); ++target)
                        {
                            if (!std::isfinite(weights[target]))
                                throw std::runtime_error("Invalid morph weight");
                            const auto delta = p.targets[target].find(semantic);
                            if (delta != p.targets[target].end() && weights[target] != 0)
                                base += float(weights[target]) * vec3(gltf, delta->second, vertex);
                        }
                        return base;
                    };
                    auto positionAt = [&](size_t vertex) {
                        return morph("POSITION", vec3(gltf, position, vertex), vertex);
                    };
                    const auto inputCount =
                      p.indices >= 0 ? gltf.accessors.at(p.indices).count : gltf.accessors.at(position).count;
                    const bool pointList = p.mode == TINYGLTF_MODE_POINTS;
                    const bool line = p.mode >= TINYGLTF_MODE_LINE && p.mode <= TINYGLTF_MODE_LINE_STRIP;
                    const bool triangleList = p.mode == TINYGLTF_MODE_TRIANGLES;
                    if (inputCount < (pointList ? 1 : line ? 2 : 3)
                        || (triangleList && inputCount % 3) || (p.mode == TINYGLTF_MODE_LINE && inputCount % 2))
                        throw std::runtime_error("Invalid primitive element count");
                    const auto primitiveCount = pointList ? inputCount
                      : p.mode == TINYGLTF_MODE_LINE ? inputCount / 2
                      : p.mode == TINYGLTF_MODE_LINE_LOOP ? inputCount
                      : line ? inputCount - 1 : triangleList ? inputCount / 3 : inputCount - 2;
                    const size_t stride = pointList || line ? 6 : 3;
                    if (primitiveCount > (10000000 - result->vertices.size()) / stride)
                        throw std::runtime_error("Oversized model mesh");
                    const auto count = primitiveCount * stride;
                    const auto determinant = glm::determinant(glm::mat3(matrix));
                    const auto mirrored = determinant < 0;
                    const float volumeScale = material.extensions.count("KHR_materials_volume")
                      ? std::cbrt(std::abs(determinant)) : 1.f;
                    constexpr std::array<size_t, 6> lineEndpoint{0, 1, 0, 0, 1, 1};
                    auto vertexIndex = [&](size_t i) {
                        const auto primitive = i / stride;
                        const auto corner = i % stride;
                        if (pointList)
                            i = primitive;
                        else if (line)
                            i = p.mode == TINYGLTF_MODE_LINE ? primitive * 2 + lineEndpoint[corner]
                                                            : (primitive + lineEndpoint[corner]) % inputCount;
                        else if (p.mode == TINYGLTF_MODE_TRIANGLE_STRIP)
                            i = primitive + (primitive % 2 && corner < 2 ? 1 - corner : corner);
                        else if (p.mode == TINYGLTF_MODE_TRIANGLE_FAN)
                            i = corner == 0 ? 0 : primitive + corner;
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
                        const auto corner = i % stride;
                        if (stride == 3 && !triangleList && corner == 0 && p.indices >= 0)
                        {
                            const auto a = vertexIndex(i), b = vertexIndex(i + 1), c = vertexIndex(i + 2);
                            if (a == b || b == c || c == a)
                            {
                                i += 2;
                                continue;
                            }
                        }
                        const auto windingIndex = mirrored && stride == 3 && corner != 0 ? i - corner + (3 - corner) : i;
                        const auto v = vertexIndex(windingIndex);
                        glm::vec3  localNormal;
                        if (normal >= 0)
                            localNormal = morph("NORMAL", vec3(gltf, normal, v), v);
                        else if (stride == 6)
                            localNormal = glm::vec3(0, 0, 1);
                        else
                        {
                            const auto first = i - i % 3;
                            const auto a     = positionAt(vertexIndex(first)),
                                       b     = positionAt(vertexIndex(first + 1)),
                                       c     = positionAt(vertexIndex(first + 2));
                            localNormal      = glm::normalize(glm::cross(b - a, c - a));
                        }
                        const auto localPosition = positionAt(v);
                        const glm::vec3 pos = glm::vec3(matrix * glm::vec4(localPosition, 1));
                        const glm::vec3 n   = glm::normalize(normals * localNormal);
                        const glm::vec2 t = uvAccessors[0] >= 0
                          ? transforms[0].first + transforms[0].second * vec2(gltf, uvAccessors[0], v) : glm::vec2(0);
                        const glm::vec2 t1 = uvAccessors[1] >= 0
                          ? transforms[1].first + transforms[1].second * vec2(gltf, uvAccessors[1], v) : t;
                        for (int k = 0; k < 3; ++k)
                            if (!std::isfinite(pos[k]) || !std::isfinite(n[k]))
                                throw std::runtime_error("Non-finite model data");
                        if (!std::isfinite(t.x) || !std::isfinite(t.y) || !std::isfinite(t1.x) || !std::isfinite(t1.y))
                            throw std::runtime_error("Non-finite texture coordinates");
                        minimum = glm::min(minimum, pos);
                        maximum = glm::max(maximum, pos);
                        if (animation)
                        {
                            AppendSkinVertex(*animation, gltf, node.skin, index, p, v, matrix,
                              vec3(gltf, position, v), normal >= 0 ? vec3(gltf, normal, v) : localNormal);
                            auto& deltas = animation->vertices.back().morphDeltas;
                            for (const auto& target : p.targets)
                            {
                                auto readDelta = [&](const char* semantic) {
                                    const auto it = target.find(semantic);
                                    return it == target.end() ? glm::vec3(0) : vec3(gltf, it->second, v);
                                };
                                deltas.push_back({readDelta("POSITION"), readDelta("NORMAL")});
                            }
                        }
                        const auto color = colorIndex == p.attributes.end()
                                             ? glm::vec4(1) : vertexColor(gltf, colorIndex->second, v);
                        result->vertices.push_back(
                          {{pos.x, pos.y, pos.z, float(materialIndex + 1)},
                           {n.x, n.y, n.z, glass[0]},
                           {t.x, t.y, normal < 0 ? 1.0f : 0.0f, glass[1]},
                           {t1.x, t1.y, stride == 6 ? float(corner + 1) : 0.0f,
                            stride == 6 ? (line ? 2.0f : 1.0f) : volumeScale},
                           {color.x, color.y, color.z, color.w}});
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
        if (gltf.materials.size() > 128)
            throw std::runtime_error("At most 128 material slots are supported");
        for (const auto& sourceMaterial : gltf.materials)
        {
            Material slot;
            slot.name       = sourceMaterial.name;
            const auto& mat = sourceMaterial;
            const auto& pbr = mat.pbrMetallicRoughness;
            if (mat.alphaMode != "OPAQUE" && mat.alphaMode != "MASK" && mat.alphaMode != "BLEND")
                throw std::runtime_error("Invalid glTF alpha mode");
            const auto specular = mat.extensions.find("KHR_materials_specular");
            if (specular != mat.extensions.end())
            {
                const auto& extension = specular->second;
                if (!extension.IsObject())
                    throw std::runtime_error("Invalid KHR_materials_specular");
                if (extension.Has("specularTexture") || extension.Has("specularColorTexture"))
                    throw std::runtime_error("KHR_materials_specular textures are not supported");
                if (extension.Has("specularFactor"))
                {
                    const auto& value = extension.Get("specularFactor");
                    if (!value.IsNumber())
                        throw std::runtime_error("Invalid glTF specularFactor");
                    slot.specularFactor = float(value.GetNumberAsDouble());
                }
                if (extension.Has("specularColorFactor"))
                {
                    const auto& color = extension.Get("specularColorFactor");
                    if (!color.IsArray() || color.ArrayLen() != 3)
                        throw std::runtime_error("Invalid glTF specularColorFactor");
                    for (int k = 0; k < 3; ++k)
                    {
                        if (!color.Get(k).IsNumber())
                            throw std::runtime_error("Invalid glTF specularColorFactor");
                        slot.specularColorFactor[k] = float(color.Get(k).GetNumberAsDouble());
                    }
                }
            }
            std::array<CoatTexture, 3> coatMaps;
            const auto coat = mat.extensions.find("KHR_materials_clearcoat");
            if (coat != mat.extensions.end())
            {
                const auto& extension = coat->second;
                if (!extension.IsObject())
                    throw std::runtime_error("Invalid KHR_materials_clearcoat");
                for (size_t k = 0; k < coatMaps.size(); ++k)
                {
                    coatMaps[k]                = readCoatTexture(extension, k);
                    slot.clearcoatTexCoords[k] = coatMaps[k].uv;
                }
                slot.clearcoatNormalScale = coatMaps[2].scale;
                for (const auto& [key, factor] :
                     {std::pair{"clearcoatFactor", &slot.clearcoatFactor},
                      std::pair{"clearcoatRoughnessFactor", &slot.clearcoatRoughnessFactor}})
                {
                    if (!extension.Has(key))
                        continue;
                    const auto& value = extension.Get(key);
                    if (!value.IsNumber())
                        throw std::runtime_error(std::string("Invalid glTF ") + key);
                    *factor = float(value.GetNumberAsDouble());
                }
            }
            slot.alphaMode   = mat.alphaMode == "MASK"    ? AlphaMode::Mask
                               : mat.alphaMode == "BLEND" ? AlphaMode::Blend
                                                          : AlphaMode::Opaque;
            slot.alphaCutoff = float(mat.alphaCutoff);
            slot.doubleSided = mat.doubleSided;
            slot.unlit = mat.extensions.count("KHR_materials_unlit") != 0;
            if (pbr.baseColorTexture.index >= 0)
            {
                const auto samplerIndex = gltf.textures.at(pbr.baseColorTexture.index).sampler;
                if (samplerIndex >= 0)
                {
                    const auto& sampler = gltf.samplers.at(samplerIndex);
                    auto wrap = [](int mode) { return mode == 33071 ? 1u : mode == 33648 ? 2u : 0u; };
                    slot.baseColorWrap = wrap(sampler.wrapS) | (wrap(sampler.wrapT) << 2);
                }
            }
            slot.emissiveTexCoord = mat.emissiveTexture.texCoord == 1
              && (pbr.baseColorTexture.index >= 0 || pbr.metallicRoughnessTexture.index >= 0
                  || mat.normalTexture.index >= 0) ? 1 : 0;
            std::array<float, 4> factors;
            for (int k = 0; k < 4; ++k)
                slot.baseColor[k] = static_cast<float>(pbr.baseColorFactor[k]);
            factors = {
              static_cast<float>(pbr.metallicFactor),
              static_cast<float>(pbr.roughnessFactor),
              static_cast<float>(mat.normalTexture.scale),
              static_cast<float>(mat.occlusionTexture.strength)};
            double emissiveStrength = 1;
            if (const auto ext = mat.extensions.find("KHR_materials_emissive_strength"); ext != mat.extensions.end())
            {
                if (!ext->second.IsObject())
                    throw std::runtime_error("Invalid KHR_materials_emissive_strength");
                if (ext->second.Has("emissiveStrength"))
                {
                    const auto& strength = ext->second.Get("emissiveStrength");
                    if (!strength.IsNumber())
                        throw std::runtime_error("Invalid glTF emissiveStrength");
                    emissiveStrength = strength.GetNumberAsDouble();
                    if (!std::isfinite(emissiveStrength) || emissiveStrength < 0)
                        throw std::runtime_error("Invalid glTF emissiveStrength");
                }
            }
            for (int k = 0; k < 3; ++k)
                slot.emissive[k] = static_cast<float>(mat.emissiveFactor[k] * emissiveStrength);
            if (mat.occlusionTexture.texCoord < 0 || mat.occlusionTexture.texCoord > 1)
                throw std::runtime_error("AO texture coordinate set must be 0 or 1");
            slot.occlusionTexCoord = uint32_t(mat.occlusionTexture.texCoord);
            const auto& volume = materialExtension(mat, "KHR_materials_volume");
            const auto& transmission = materialExtension(mat, "KHR_materials_transmission");
            auto number = [&](const char* key, float fallback) {
                if (!volume.Has(key))
                    return fallback;
                const auto& v = volume.Get(key);
                if (!v.IsNumber() || !std::isfinite(float(v.GetNumberAsDouble())) || v.GetNumberAsDouble() < 0)
                    throw std::runtime_error("Invalid glTF volume parameter");
                return float(v.GetNumberAsDouble());
            };
            slot.thicknessFactor = number("thicknessFactor", 0);
            if (volume.Has("attenuationDistance"))
            {
                const auto distance = number("attenuationDistance", 0);
                if (distance <= 0)
                    throw std::runtime_error("Attenuation distance must be positive");
                slot.inverseAttenuationDistance = 1 / distance;
            }
            if (volume.Has("attenuationColor"))
            {
                const auto& color = volume.Get("attenuationColor");
                if (!color.IsArray() || color.ArrayLen() != 3)
                    throw std::runtime_error("Invalid attenuation color");
                for (size_t k = 0; k < 3; ++k)
                {
                    if (!color.Get(k).IsNumber())
                        throw std::runtime_error("Invalid attenuation color");
                    slot.attenuationColor[k] = float(color.Get(k).GetNumberAsDouble());
                }
            }
            const int textures[]   = {
              pbr.baseColorTexture.index,
              mat.normalTexture.index,
              pbr.metallicRoughnessTexture.index,
              mat.occlusionTexture.index,
              mat.emissiveTexture.index,
              coatMaps[0].index,
              coatMaps[1].index,
              coatMaps[2].index,
              volumeTextureIndex(transmission, "transmissionTexture"),
              volumeTextureIndex(volume, "thicknessTexture")};
            std::optional<Image>* maps[] = {
              &slot.baseColorTexture,
              &slot.normalTexture,
              &slot.metallicRoughnessTexture,
              &slot.occlusionTexture,
              &slot.emissiveTexture,
              &slot.clearcoatTexture,
              &slot.clearcoatRoughnessTexture,
              &slot.clearcoatNormalTexture,
              &slot.transmissionTexture,
              &slot.thicknessTexture};
            slot.metallic          = factors[0];
            slot.roughness         = factors[1];
            slot.normalScale       = factors[2];
            slot.occlusionStrength = factors[3];
            for (size_t i = 0; i < std::size(textures); ++i)
            {
                ModelImage dst;
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
                *maps[i]   = std::move(dst);
            }
            ValidateMaterial(slot);
            result->materials.push_back(std::move(slot));
        }
        PackMaterials(*result);
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
            const std::array<std::array<unsigned char, 4>, 8> colors = {
              {{255, 255, 255, 255},
               {128, 128, 255, 255},
               {255, 255, 255, 255},
               {255, 255, 255, 255},
               {0, 0, 0, 255},
               {255, 255, 255, 255},
               {255, 255, 255, 255},
               {128, 128, 255, 255}}};
            for (size_t i = 0; i < colors.size(); ++i)
                data->images[i].pixels.assign(colors[i].begin(), colors[i].end());
            return data;
        }();
        return empty;
    }
    SceneUniforms MakeSceneUniforms(
      const ModelData&           data,
      const Camera&              camera,
      float                      aspect,
      LightingMode               lighting,
      const DirectionalLight&    light,
      const EnvironmentData*     environment,
      const EnvironmentSettings& settings)
    {
        const auto      c = ValidateViewCamera(camera);
        const glm::vec3 eye(c.position[0], c.position[1], c.position[2]);
        const glm::quat rotation(c.orientation[3], c.orientation[0], c.orientation[1], c.orientation[2]);
        const auto      right = rotation * glm::vec3(1, 0, 0), up = rotation * glm::vec3(0, 1, 0),
                   forward     = rotation * glm::vec3(0, 0, -1);
        const float halfHeight = c.projection == Projection::Orthographic ? c.orthographicHeight * .5f
                                                                          : std::tan(glm::radians(c.verticalFov) * .5f);
        const auto  projection =
          c.projection == Projection::Orthographic
             ? glm::orthoRH_ZO(
                -halfHeight * aspect, halfHeight * aspect, -halfHeight, halfHeight, c.nearPlane, c.farPlane)
             : glm::perspectiveRH_ZO(glm::radians(c.verticalFov), aspect, c.nearPlane, c.farPlane);
        const auto    vp = projection * glm::translate(glm::mat4_cast(glm::conjugate(rotation)), -eye);
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
        for (int k = 0; k < 3; ++k)
        {
            result.cameraRight[k]   = right[k];
            result.cameraUp[k]      = up[k];
            result.cameraForward[k] = forward[k];
        }
        result.cameraRight[3]       = halfHeight * aspect;
        result.cameraUp[3]          = halfHeight;
        result.cameraForward[3]     = environment ? 1.0f : 0.0f;
        result.environmentParams[0] = std::sin(glm::radians(settings.rotation));
        result.environmentParams[1] = std::cos(glm::radians(settings.rotation));
        result.environmentParams[2] = settings.intensity;
        result.environmentParams[3] = environment ? float(environment->mips.size() - 1) : 0;
        result.skyParams[0]         = settings.backgroundIntensity;
        result.skyParams[1]         = settings.showBackground ? 1.0f : 0.0f;
        result.skyParams[3]         = !data.skinInputs.empty() ? 1.0f : 0.0f;
        if (environment)
            for (int k = 0; k < 9; ++k)
                std::copy(environment->irradiance[k].begin(), environment->irradiance[k].end(), result.irradiance[k]);
        result.irradiance[0][3] = 0; // Reserved raw-HDR flag; environment padding must not enable it.
        result.emissive[3]      = lighting == LightingMode::PBR ? 1.0f : 0.0f;
        result.cameraUp[3]      = c.projection == Projection::Orthographic ? -halfHeight : halfHeight;
        result.irradiance[1][3] = c.nearPlane;
        result.irradiance[2][3] = c.farPlane;
        return result;
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
        return MakeSceneUniforms(data, CameraFromOrbit(camera), aspect, lighting, light, environment, settings);
    }
} // namespace render::detail
