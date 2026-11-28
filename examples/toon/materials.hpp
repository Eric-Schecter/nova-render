#pragma once
#include <render/Window.hpp>

inline void ApplyLunariaToon(render::Window& window, const std::string& assetId, float mix)
{
    auto materials = window.GetMaterials(assetId);
    for (uint32_t slot = 0; slot < materials.size(); ++slot)
    {
        auto& material = materials[slot];
        if (material.name == "Lunaria_Skin_Anime")
        {
            material.lighting = render::MaterialLighting::ToonSkin;
            material.toon = render::ToonLighting::Skin();
            material.toon->skinSSS = .85f;
        }
        else if (material.name == "Lunaria_Outfit_PBR")
        {
            material.lighting = render::MaterialLighting::ToonSurface;
            material.toon = render::ToonLighting{};
        }
        else
            continue;
        material.toon->toonMix = mix;
        material.toon->rimStrength = .65f;
        window.SetMaterial(slot, material, assetId);
    }
}
