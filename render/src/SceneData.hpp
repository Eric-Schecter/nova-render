#pragma once
#include "AnimationData.hpp"
#include "ModelData.hpp"
#include "Picking.hpp"
#include "render/Scene.hpp"
#include <map>
#include <tuple>
namespace render::detail
{
    struct InstanceData
    {
        float world[16], normal[16], baseColor[4], material[4], emissive[4], uv[4];
    };
    static_assert(sizeof(InstanceData) == 192, "Instance shader layout");
    struct SceneDraw
    {
        uint32_t firstVertex, vertexCount, firstInstance, instanceCount;
    };
    class SceneData
    {
      public:
        std::vector<Material>            GetMaterials(const std::string& id) const;
        void                             SetMaterial(const std::string& id, uint32_t slot, const Material& material);
        std::shared_ptr<const ModelData> RayModel();
        std::optional<Bounds>            InstanceBounds(const std::string&) const;
        bool                             HasAnimations() const { return active && !players.empty(); }
        std::vector<AnimationClip>       Animations(const std::string& assetId) const;
        AnimationState                   Animation(const std::string& instanceId) const;
        void                             SetAnimation(const std::string& instanceId, const AnimationState&);
        void                             UpdateAnimations(float delta);
        bool                             active = false;
        std::shared_ptr<const ModelData> model;
        std::vector<InstanceData>        instances;
        std::vector<InstanceData>        shadowRecords;
        void                   AppendAnimatedShadows(std::vector<InstanceData>& records, size_t shadowBase) const;
        std::vector<SceneDraw> draws;
        std::vector<std::string> drawInstanceIds;
        // Asset topology changes rebuild packed resources; transform updates only replace instance data.
        std::optional<SceneHit> Pick(const Ray& ray) const;
        std::optional<Bounds>   AssetBounds(const std::string& id) const;
        void                    Update(const std::vector<SceneAsset>& assets, const std::vector<SceneInstance>& input);

      private:
        std::shared_ptr<const ModelData> rayModel, raySource;
        std::vector<InstanceData>        rayInstances;
        std::vector<SceneAsset>          assetsSnapshot;
        std::vector<SceneInstance>       inputSnapshot;
        struct Player
        {
            std::string                              assetId;
            std::filesystem::path                    path;
            AnimationState                           state;
            uint32_t                                 paletteOffset = 0;
            std::vector<JointMatrix>                 palette;
            mutable std::shared_ptr<const ModelData> query;
            mutable std::shared_ptr<PickMesh>        queryPick;
        };
        std::map<std::string, Player>    players;
        void                             CommitPalettes();
        std::shared_ptr<const ModelData> PoseGeometry(const std::string&) const;

        void Build(const std::vector<SceneAsset>& assets, const std::vector<SceneInstance>& input);
        std::map<std::string, bool> sourceSpaces;
        std::vector<SceneInstance>  pickInstances;
        struct Mesh
        {
            std::shared_ptr<const ModelData> data;
            uint32_t                         first, count;
            uint32_t                         shadowFirst = 0, shadowNodes = 0;
            std::array<float, 4>             uv;
            std::shared_ptr<const PickMesh>  picking;
        };
        using AssetKey = std::tuple<std::string, std::filesystem::path, std::optional<uint32_t>, std::shared_ptr<const SceneGeometry>>;
        std::vector<AssetKey> key;
        std::map<std::string, Mesh>                                meshes;
    };
} // namespace render::detail
