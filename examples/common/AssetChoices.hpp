#pragma once
#include <render/ParameterPanel.hpp>
namespace example
{
    inline std::string AssetName(const std::filesystem::path& path)
    {
        const auto name = path.stem().string();
        if (name == "CornellBox")
            return "Cornell Box";
        if (name == "DamagedHelmet")
            return "Damaged Helmet";
        if (name == "venice_sunset")
            return "Venice Sunset";
        if (name == "kloppenheim_06")
            return "Kloppenheim 06";
        if (name == "studio_small_09")
            return "Studio Small 09";
        return name;
    }
    inline std::vector<render::AssetChoice> ModelChoices(const std::filesystem::path& current)
    {
        const auto                       assets = current.parent_path().parent_path();
        std::vector<render::AssetChoice> result{{AssetName(current), current}};
        for (const auto* name : {"DamagedHelmet", "Avocado", "BoomBox", "CornellBox"})
        {
            const auto path = assets / name / (std::string(name) + ".glb");
            if (path != current && std::filesystem::exists(path))
                result.push_back({AssetName(path), path});
        }
        return result;
    }
    inline std::vector<render::AssetChoice> EnvironmentChoices(const std::filesystem::path& current)
    {
        std::vector<render::AssetChoice> result{{AssetName(current), current}};
        for (const auto* name : {"venice_sunset", "kloppenheim_06", "studio_small_09"})
        {
            const auto path = current.parent_path() / (std::string(name) + ".nenv");
            if (path != current && std::filesystem::exists(path))
                result.push_back({AssetName(path), path});
        }
        return result;
    }
} // namespace example
