#pragma once
#include "ModelData.hpp"
#include "Picking.hpp"
#include "render/Scene.hpp"
#include <map>
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
        bool                             active = false;
        std::shared_ptr<const ModelData> model;
        std::vector<InstanceData>        instances;
        std::vector<SceneDraw>           draws;
        // Asset topology changes rebuild packed resources; transform updates only replace instance data.
        std::optional<SceneHit> Pick(const Ray& ray) const;
        std::optional<Bounds> AssetBounds(const std::string& id) const;
        void                  Update(const std::vector<SceneAsset>& assets, const std::vector<SceneInstance>& input);

      private:
        std::vector<SceneAsset>     assetsSnapshot;
        std::vector<SceneInstance>  inputSnapshot;
        void Build(const std::vector<SceneAsset>& assets, const std::vector<SceneInstance>& input);
        std::map<std::string, bool> sourceSpaces;
        std::vector<SceneInstance>  pickInstances;
        struct Mesh
        {
            std::shared_ptr<const ModelData> data;
            uint32_t                         first, count;
            std::array<float, 4>             uv;
            std::shared_ptr<const PickMesh>  picking;
        };
        std::vector<std::pair<std::string, std::filesystem::path>> key;
        std::map<std::string, Mesh>                                meshes;
    };
} // namespace render::detail
