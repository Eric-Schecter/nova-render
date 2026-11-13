#pragma once
namespace render {
// Models show base color by default; PBR enables the built-in studio lighting.
enum class LightingMode { Unlit, PBR };
// Orbit about the normalized model center. Angles are degrees, distance > 0.
struct OrbitCamera {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float distance = 3.6f;
    float exposure = 1.0f; // PBR only; Unlit displays base color unchanged.
};
}
