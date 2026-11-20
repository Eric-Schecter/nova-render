#pragma once
#include <render/Window.hpp>
#include <vector>

namespace example
{
    inline std::vector<render::SceneLight> ShowroomLights(bool warmSideLighting)
    {
        using render::LightType;
        using render::SceneLight;
        SceneLight hood;
        hood.id         = "hood";
        hood.type       = LightType::Rectangle;
        hood.position   = {-.4f, 3.25f, 1.15f};
        hood.direction  = {0, -1, 0};
        hood.up         = {0, 0, 1};
        hood.width      = 4.7f;
        hood.height     = 1.4f;
        hood.intensity  = 2.5f;
        hood.color      = {1, .94f, .86f};
        SceneLight side = hood;
        side.id         = "side";
        side.position   = {3.15f, 1.25f, -.35f};
        side.direction  = {-1, -.15f, 0};
        side.up         = {0, 1, 0};
        side.width      = 5.1f;
        side.height     = .55f;
        side.intensity  = 3.2f;
        side.color      = {.88f, .95f, 1};
        hood.castShadow = true;
        side.castShadow = false;
        SceneLight fill = side;
        fill.id         = "fill";
        fill.position   = {2.9f, .85f, -.65f};
        fill.direction  = {-1, .14f, -.08f};
        fill.width      = 5.8f;
        fill.height     = 1.35f;
        fill.intensity  = .55f;
        fill.color      = {1, 1, 1};
        if (warmSideLighting)
        {
            hood.intensity = 1.65f;
            hood.color     = {.58f, .76f, 1.f};
            side.position  = {3.15f, 1.55f, -1.15f};
            side.direction = {-1.f, -.22f, .08f};
            side.width     = 5.7f;
            side.height    = .7f;
            side.intensity = 5.f;
            side.color     = {1.f, .65f, .32f};
            fill.intensity = .35f;
            fill.color     = {.63f, .78f, 1.f};
        }
        return {hood, side, fill};
    }
} // namespace example
