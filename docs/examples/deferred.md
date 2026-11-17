# Deferred model rendering

This example uses the formal model renderer, with the same GLB loading, materials, instances, GPU skinning and lighting APIs as other model examples. Open Parameters to compare Forward / Deferred and inspect the G-buffer.

<ClientOnly><ModelPlayground deferred /></ClientOnly>

The geometry pass writes four GPU-resident targets. A full-screen pass then uses the same PBR lighting function as Forward, including light collections, scene shadows, environment lighting, emissive materials and fog. The raster pipeline is independent of Raster / Ray tracing mode.

Deferred supports Off, FXAA, SMAA, SSAA and TAA. MSAA is explicitly unavailable until per-sample deferred lighting is implemented. G-buffer diagnostic views bypass AA and post-processing, while retaining the application's settings for Final lighting. [Model raster API](../api/raster).
