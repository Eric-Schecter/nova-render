#pragma once
#include "Lighting.hpp"
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <render/Window.hpp>

namespace example
{
    inline void DrawProbeOverlay(const render::Window& window, const render::HybridProbeBounds& bounds,
                                 bool probeGI, bool& visible, ShowroomLighting& lighting)
    {
        ImGui::SetNextWindowPos({12, 12}, ImGuiCond_Once);
        ImGui::SetNextWindowBgAlpha(.75f);
        if (ImGui::Begin("Showroom", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
        {
            int selected = int(lighting);
            if (ImGui::Combo("Lighting", &selected, "Quarry daylight\0Warm side\0Studio\0"))
                lighting = ShowroomLighting(selected);
            if (probeGI)
                ImGui::Checkbox("Show DDGI probes", &visible);
        }
        ImGui::End();
        if (!visible)
            return;
        const auto view         = window.GetViewCamera();
        const auto [x, y, z, w] = view.orientation;
        const glm::quat inverse = glm::conjugate(glm::quat(w, x, y, z));
        const glm::vec3 eye(view.position[0], view.position[1], view.position[2]);
        const auto      size  = ImGui::GetIO().DisplaySize;
        const float     focal = .5f * size.y / std::tan(glm::radians(view.verticalFov) * .5f);
        const auto      grid  = render::HybridProbeGridDimensions;
        const ImU32     colors[]{
          IM_COL32(106, 225, 255, 235),
          IM_COL32(123, 255, 187, 235),
          IM_COL32(255, 223, 132, 235),
          IM_COL32(233, 161, 255, 235)};
        // Use the background draw list so the checkbox always stays readable.
        auto* draw = ImGui::GetBackgroundDrawList();
        for (uint32_t j = 0; j < grid[1]; ++j)
            for (uint32_t k = 0; k < grid[2]; ++k)
                for (uint32_t i = 0; i < grid[0]; ++i)
                {
                    const uint32_t indices[]{i, j, k};
                    glm::vec3      world;
                    for (int axis = 0; axis < 3; ++axis)
                        world[axis] =
                          bounds.minimum[axis]
                          + (indices[axis] + .5f) * (bounds.maximum[axis] - bounds.minimum[axis]) / grid[axis];
                    const glm::vec3 p = inverse * (world - eye);
                    if (-p.z < view.nearPlane || -p.z > view.farPlane)
                        continue;
                    const ImVec2 center{size.x * .5f - focal * p.x / p.z, size.y * .5f + focal * p.y / p.z};
                    draw->AddCircleFilled(center, 4.0f, IM_COL32(12, 17, 24, 220));
                    draw->AddCircleFilled(center, 2.75f, colors[j]);
                }
    }
} // namespace example
