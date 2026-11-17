# Hybrid rendering

Raster primary visibility writes the material G-buffer. A separate GPU BVH pass traces per-light shadow visibility, which the deferred PBR pass combines with direct and environment lighting. Native presentation samples the final GPU texture directly.

<ClientOnly><ModelPlayground hybrid /></ClientOnly>

Use Parameters to switch Raster / Hybrid, toggle shadows, AO, reflections and temporal reconstruction, adjust ray budgets and inspect effect views. The metallic floor reflects the box; AO emphasizes their contact, and temporal reconstruction reduces sampling noise. Native: `nova_hybrid assets/SceneSample/Red.glb --api metal` or `vulkan`.

Shared HLSL implements shadows, AO and one-bounce GGX reflections through a GPU shader BVH, followed by temporal reconstruction. DDGI and hardware ray queries remain unavailable. The example uses self-authored geometry; original reference scenes/cameras are tested separately. [Hybrid API](../api/hybrid) · [Validation](../validation/hybrid).
