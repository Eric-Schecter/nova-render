#pragma once
#include "render/Model.hpp"
#include <array>
#include <filesystem>
#include <memory>
#include <vector>
#include <cstdint>
namespace render::detail {
struct ModelVertex { float position[4]; float normal[4]; float uv[4]; };
struct ModelImage { uint32_t width=1, height=1; std::vector<unsigned char> pixels; };
struct ModelData {
    std::vector<ModelVertex> vertices;
    // Base color, normal, metallic/roughness, occlusion, emissive.
    std::array<ModelImage,5> images;
    std::array<float,4> baseColor{1,1,1,1};
    std::array<float,4> material{1,1,1,1}; // metallic, roughness, normal scale, AO strength
    std::array<float,4> emissive{0,0,0,0};
};
struct SceneUniforms {
    float viewProjection[16];
    float eyeExposure[4];
    float baseColor[4];
    float material[4];
    float emissive[4];
};
std::shared_ptr<const ModelData> LoadGLB(const std::filesystem::path& path);
void ValidateCamera(const OrbitCamera& camera);
SceneUniforms MakeSceneUniforms(const ModelData& data, const OrbitCamera& camera, float aspect);
}
