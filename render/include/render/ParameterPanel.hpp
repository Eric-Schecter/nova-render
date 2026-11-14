#pragma once
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
        bool                  showBuiltinPanel = true;
    };
} // namespace render
