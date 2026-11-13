#pragma once
namespace render {
// Orbit about the normalized model center. Angles are degrees, distance > 0.
struct OrbitCamera {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float distance = 3.6f;
    float exposure = 1.0f;
};
}
