# Hybrid rendering

Raster primary visibility writes the material G-buffer. A separate GPU BVH pass traces per-light shadow visibility, which the deferred PBR pass combines with direct and environment lighting. Native presentation samples the final GPU texture directly.

<ClientOnly><ModelPlayground hybrid /></ClientOnly>

Use Parameters to switch Raster / Hybrid, toggle shadows, AO, reflections and temporal reconstruction, adjust ray budgets and inspect effect views. Choose a reference scene and one of four camera presets, or Box and mirror. Scenes load on demand; drag to orbit or use Reset view. Native: `nova_hybrid assets/SceneSample/Red.glb --api metal` or `vulkan`.

Shared HLSL implements shadows, AO and one-bounce GGX reflections through a GPU shader BVH, followed by temporal reconstruction. DDGI and hardware ray queries remain unavailable. Reference scenes retain their original geometry, materials and camera presets under a shared validation light; they do not reproduce the reference application’s DDGI. Resources are credited in the footer. [Hybrid API](../api/hybrid) · [Validation](../validation/hybrid).
