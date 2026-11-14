#pragma once
#include "ModelData.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
// Test-only oracle. Production animation sampling never loops over vertices per frame.
inline std::vector<render::detail::ModelVertex> ReferenceSkin(const render::detail::ModelData& model)
{
    auto result = model.vertices;
    for (size_t i = 0; i < model.skinInputs.size(); ++i)
    {
        const auto& input = model.skinInputs[i];
        glm::mat4   matrix(0);
        for (int k = 0; k < 4; ++k)
            matrix += input.weights[k] * glm::make_mat4(model.jointMatrices.at(input.joints[k]).data());
        auto position = matrix * glm::make_vec4(input.position.data());
        auto normal   = glm::normalize(
          glm::transpose(glm::inverse(glm::mat3(matrix))) * glm::vec3(glm::make_vec4(input.normal.data())));
        for (int k = 0; k < 3; ++k)
        {
            result[i].position[k] = position[k];
            result[i].normal[k]   = normal[k];
        }
    }
    return result;
}
