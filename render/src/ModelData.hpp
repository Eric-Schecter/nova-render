#pragma once
#include "EnvironmentData.hpp"
#include "render/Atmosphere.hpp"
#include "render/Image.hpp"
#include "render/Material.hpp"
#include "render/Model.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>
namespace render::detail
{
    struct ModelVertex
    {
        float position[4]; // xyz: position; w: one-based material slot (shader restores homogeneous w).
        float normal[4]; // xyz: normal; w: dielectric transmission fraction.
        float uv[4];     // xy: UV; z: generated flat-normal flag; w: dielectric IOR.
    };
    using ModelImage = render::Image;
    struct SkinInput
    {
        std::array<float, 4>    position{}, normal{};
        std::array<uint32_t, 4> joints{};
        std::array<float, 4>    weights{1, 0, 0, 0};
    };
    static_assert(sizeof(SkinInput) == 64, "HLSL skin input layout");
    using JointMatrix = std::array<float, 16>;
    struct AnimationData;
    struct ModelData
    {
        std::shared_ptr<const AnimationData> animation;
        uint64_t                             revision = 0;
        std::array<float, 3>                 sourceMinimum{}, sourceMaximum{};
        std::vector<ModelVertex>             vertices;
        std::vector<Material>                materials;
        // GPU material records use the existing 192-byte instance layout.
        std::vector<std::array<float, 48>>   materialRecords;
        std::vector<SkinInput>               skinInputs;
        std::vector<JointMatrix>             jointMatrices;
        // Base color, normal, metallic/roughness, occlusion, emissive.
        std::array<ModelImage, 5> images;
        std::array<float, 4>      baseColor{1, 1, 1, 1};
        std::array<float, 4>      material{1, 1, 1, 1}; // metallic, roughness, normal scale, AO strength
        std::array<float, 4>      emissive{0, 0, 0, 0};
    };
    inline bool HasBlend(const ModelData* data)
    {
        if (data)
            for (const auto& m : data->materials)
                if (m.alphaMode == AlphaMode::Blend)
                    return true;
        return false;
    }
    inline bool HasAlpha(const ModelData* data)
    {
        if (data)
            for (const auto& m : data->materials)
                if (m.alphaMode != AlphaMode::Opaque)
                    return true;
        return false;
    }
    struct SceneUniforms
    {
        float viewProjection[16];
        float eyeExposure[4];
        float baseColor[4];
        float material[4];
        float emissive[4]; // rgb: emissive factor, w: PBR enabled
        float lightDirection[4];
        float lightColorIntensity[4];
        float cameraRight[4];
        float cameraUp[4];
        float cameraForward[4];     // w: an environment is loaded
        float environmentParams[4]; // sin/cos rotation, intensity, highest mip
        float skyParams[4];         // background intensity, visibility
        float irradiance[9][4];
    };
    static_assert(sizeof(ModelVertex) == 48, "HLSL vertex storage layout must match");
    static_assert(sizeof(SceneUniforms) == 384, "HLSL scene uniform layout must match");
    void                             PackMaterials(ModelData& data);
    void                             ValidateMaterial(const Material& material);
    std::shared_ptr<const ModelData> LoadGLB(const std::filesystem::path& path);
    std::shared_ptr<const ModelData> EmptyModelData();
    void                             ValidateDirectionalLight(const DirectionalLight& light);
    void                             ValidateCamera(const OrbitCamera& camera);
    SceneUniforms                    MakeSceneUniforms(
                         const ModelData&           data,
                         const Camera&              camera,
                         float                      aspect,
                         LightingMode               lighting    = LightingMode::Unlit,
                         const DirectionalLight&    light       = {},
                         const EnvironmentData*     environment = nullptr,
                         const EnvironmentSettings& settings    = {});
    SceneUniforms                    MakeSceneUniforms(
                         const ModelData&           data,
                         const OrbitCamera&         camera,
                         float                      aspect,
                         LightingMode               lighting    = LightingMode::Unlit,
                         const DirectionalLight&    light       = {},
                         const EnvironmentData*     environment = nullptr,
                         const EnvironmentSettings& settings    = {});
} // namespace render::detail
