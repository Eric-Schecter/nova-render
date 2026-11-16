#pragma once
#include "ModelData.hpp"
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
        bool                             active = false;
        std::shared_ptr<const ModelData> model;
        std::vector<InstanceData>        instances;
        std::vector<SceneDraw>           draws;
        // Asset topology changes rebuild packed resources; transform updates only replace instance data.
        std::optional<Bounds> AssetBounds(const std::string& id) const;
        void                  Update(const std::vector<SceneAsset>& assets, const std::vector<SceneInstance>& input);

      private:
        void Build(const std::vector<SceneAsset>& assets, const std::vector<SceneInstance>& input);
        std::map<std::string, bool> sourceSpaces;
        struct Mesh
        {
            std::shared_ptr<const ModelData> data;
            uint32_t                         first, count;
            std::array<float, 4>             uv;
        };
        std::vector<std::pair<std::string, std::filesystem::path>> key;
        std::map<std::string, Mesh>                                meshes;
    };
} // namespace render::detail
