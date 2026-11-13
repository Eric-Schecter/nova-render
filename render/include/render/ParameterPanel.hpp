#pragma once
#include <filesystem>
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
    };
} // namespace render
