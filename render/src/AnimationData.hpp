#pragma once
#include "ModelData.hpp"
#include "render/Animation.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <tiny_gltf.h>
namespace render::detail
{
    std::shared_ptr<const ModelData> PoseAnimationGeometry(
      const ModelData& source, const std::vector<JointMatrix>& palette);
    struct AnimationNode
    {
        int       parent = -1;
        glm::vec3 translation{0}, scale{1};
        glm::quat rotation{1, 0, 0, 0};
        glm::mat4 matrix{1};
        bool      hasMatrix = false;
    };
    struct AnimationTrack
    {
        int                    node;
        std::string            path, interpolation;
        std::vector<float>     times;
        std::vector<glm::vec4> values;
    };
    struct SkinVertex
    {
        glm::vec3  position, normal;
        glm::vec4  weights{1, 0, 0, 0};
        glm::uvec4 joints{0};
        int        skin = -1;
        int        node = -1;
        glm::mat4  matrix{1};
    };
    struct AnimationSkin
    {
        std::vector<int>       joints;
        std::vector<glm::mat4> inverseBind;
    };
    struct AnimationData
    {
        std::vector<AnimationNode>               nodes;
        std::vector<AnimationSkin>               skins;
        std::vector<AnimationClip>               clips;
        std::vector<std::vector<AnimationTrack>> tracks;
        std::vector<SkinVertex>                  vertices;
        glm::vec3                                center{0};
        float                                    scale = 1;
    };
    std::shared_ptr<AnimationData> ReadAnimationData(const tinygltf::Model&);
    void                           AppendSkinVertex(
                                AnimationData&,
                                const tinygltf::Model&,
                                int,
                                int,
                                const tinygltf::Primitive&,
                                uint32_t,
                                const glm::mat4&,
                                const glm::vec3&);
    std::vector<JointMatrix> EvaluateAnimation(const AnimationData&, const AnimationState&);
    AnimationState           ValidateAnimationState(const std::vector<AnimationClip>&, const AnimationState&);
    AnimationState           AdvanceAnimation(const std::vector<AnimationClip>&, AnimationState, float);
    class AnimationPlayer
    {
      public:
        void                             Load(std::shared_ptr<const ModelData>);
        std::shared_ptr<const ModelData> Model() const { return posed ? posed : source; }
        std::vector<AnimationClip>       Clips() const;
        AnimationState                   state;
        void                             Select(uint32_t);
        void                             Seek(float);
        void                             Update(float);
        void                             Apply();

      private:
        std::shared_ptr<const ModelData> source;
        std::shared_ptr<ModelData>       posed;
    };
} // namespace render::detail
