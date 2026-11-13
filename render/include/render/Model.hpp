#pragma once
#include <array>
namespace render
{
    // Models show base color by default; PBR enables studio lighting or a loaded HDR environment.
    enum class LightingMode
    {
        Unlit,
        PBR
    };
    // World-space vector from surface toward light; normalized internally.
    // Linear RGB [0,1], intensity [0,100]. Only affects PBR's main direct light.
    struct DirectionalLight
    {
        std::array<float, 3> direction{-1.0f, 1.6f, 2.0f};
        std::array<float, 3> color{1.0f, 3.2f / 3.5f, 0.8f};
        float                intensity = 3.5f;
    };
    // Rotation in degrees [-180,180], linear intensity multipliers [0,16].
    struct EnvironmentSettings
    {
        float rotation            = 0.0f;
        float intensity           = 1.0f;
        float backgroundIntensity = 1.0f;
        bool  showBackground      = true;
    };
    // Orbit about the normalized model center. Angles are degrees, distance > 0.
    struct OrbitCamera
    {
        float yaw      = 0.0f;
        float pitch    = 0.0f;
        float distance = 3.6f;
        float exposure = 1.0f; // PBR/skybox exposure; unlit model base color stays unchanged.
    };
} // namespace render
