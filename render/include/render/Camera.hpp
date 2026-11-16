#pragma once
#include <array>
#include <cstdint>

namespace render
{
    // Orbit about target with the legacy lens defaults. Angles are degrees.
    struct OrbitCamera
    {
        float                yaw      = 0.0f;
        float                pitch    = 0.0f;
        float                distance = 3.6f;
        float                exposure = 1.0f; // PBR/skybox exposure; unlit model base color stays unchanged.
        std::array<float, 3> target{0, 0, 0};
    };
    enum class Projection
    {
        Perspective,
        Orthographic
    };
    struct Camera
    {
        // Right-handed world space, local +X right, +Y up, -Z forward.
        std::array<float, 3> position{0, 0, 3.6f};
        // Local-to-world quaternion, [x,y,z,w]. Normalized by SetViewCamera.
        std::array<float, 4> orientation{0, 0, 0, 1};
        Projection           projection         = Projection::Perspective;
        float                verticalFov        = 45; // Degrees, vertical; independent of viewport aspect ratio.
        float                orthographicHeight = 3;  // Full vertical span in world units.
        float                nearPlane = .01f, farPlane = 200;
        float                exposure = 1;
    };
    Camera CameraFromOrbit(const OrbitCamera& camera);
    struct Viewport
    {
        // Top-left origin, framebuffer pixels, no implicit DPI conversion.
        uint32_t x = 0, y = 0, width = 1, height = 1;
    };
} // namespace render
