#pragma once
#ifdef NOVA_EXAMPLE_UI
#include <algorithm>
#include <imgui.h>
namespace example
{
    inline void PlaceFloatingPanel()
    {
        const auto  display = ImGui::GetIO().DisplaySize;
        const float width   = std::min(360.f, std::max(1.f, display.x - 16));
        ImGui::SetNextWindowPos({std::max(8.f, display.x - width - 12), 52});
        ImGui::SetNextWindowSize({width, std::max(1.f, std::min(430.f, display.y - 64))});
        ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
    }
} // namespace example
#endif
