# Hybrid rendering

Raster primary visibility writes the material G-buffer. A separate GPU BVH pass traces per-light shadow visibility, which the deferred PBR pass combines with direct and environment lighting. Native presentation samples the final GPU texture directly.

<ClientOnly><ModelPlayground hybrid /></ClientOnly>

Use Parameters to switch Raster / Hybrid, toggle Hybrid shadows, adjust shadow rays and inspect an individual light's visibility. Native: `nova_hybrid assets/SceneSample/Red.glb --api metal` or `vulkan`.

This first stage implements shadows through a GPU shader BVH. AO, ray-traced reflections, DDGI and temporal reconstruction are reported as unavailable. The example uses self-authored geometry; original reference scenes/cameras are tested separately. [Hybrid API](../api/hybrid) · [Validation](../validation/hybrid).
