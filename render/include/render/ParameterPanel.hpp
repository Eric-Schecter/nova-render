#pragma once
#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
namespace render
{
    struct AssetChoice
    {
        std::string           name;
        std::filesystem::path path;
    };
    struct ParameterPanelConfig
    {
        std::vector<AssetChoice> models;
        std::vector<AssetChoice> environments;
        // Called on the window thread inside an active ImGui frame.
        // Submit UI only; defer renderer mutations until after DrawFrame().
        std::function<void()> drawUI;
        // Once, before the first GPU UI frame; context is active for fonts and style.
        std::function<void()> configureUI;
        // Optional scene rectangle in UI logical pixels, sampled after drawUI. Explicit SetViewport wins.
        std::function<std::array<float, 4>()> sceneViewport;
        bool                  showBuiltinPanel = true;
        // Logical pixels reserved above the scene for application UI (requires UI build).
        float sceneTopInset = 0;
    };
} // namespace render
