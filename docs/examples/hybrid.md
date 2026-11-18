# Hybrid rendering

Raster primary visibility writes the material G-buffer. A separate GPU BVH pass traces per-light shadow visibility, which the deferred PBR pass combines with direct and environment lighting. Native presentation samples the final GPU texture directly.

<ClientOnly><ModelPlayground hybrid /></ClientOnly>

Use Parameters to switch Raster / Hybrid, toggle shadows, AO, reflections, DDGI probes, diffuse bounce and temporal reconstruction, adjust ray budgets and inspect effect views. Choose a reference scene and one of four camera presets, or Box and mirror. Scenes load on demand; drag to orbit or use Reset view. Native: `nova_hybrid assets/SceneSample/Red.glb --api metal` or `vulkan`.

Shared HLSL implements shadows, AO, one-bounce GGX reflections and one-bounce diffuse indirect light through a GPU shader BVH, followed by temporal reconstruction. Select Global Illumination Test and toggle DDGI probes to compare the cached lighting with the per-pixel Diffuse bounce mode. Hardware ray queries remain unavailable. Reference scenes retain their original geometry, materials and camera presets under a shared validation light; the shared six-lobe probe grid is an approximation of the reference application’s lighting. Resources are credited in the footer. [Hybrid API](../api/hybrid) · [Validation](../validation/hybrid).
