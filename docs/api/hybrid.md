# Hybrid rendering

Hybrid combines raster primary visibility with GPU software-BVH secondary rays. Settings, capabilities and controls match C++ and WebGPU. Available in the 0.9.0 paired SDK; published 0.8.0 predates Hybrid.

```cpp
render::HybridSettings settings;
settings.ambientOcclusion = true;
settings.reflections = true;
settings.temporalReconstruction = true;
window.SetHybrid(settings);
window.SetRenderMode(render::RenderMode::Hybrid);
```

```js
renderer.setHybrid({
  ambientOcclusion: true,
  reflections: true,
  temporalReconstruction: true,
})
renderer.setRenderMode('hybrid')
```

Load a static model or scene first. `GetHybrid()` / `getHybrid()` returns settings; `GetHybridCapabilities()` / `getHybridCapabilities()` reports supported features. Invalid changes preserve the last valid state.

## Settings

- Shadows: `shadows` defaults to true; `shadowSamples` is 1–16, default 4. Per-light softness and shadow flags apply independently. Zero softness uses one hard ray. Raster's shadow master does not disable Hybrid shadows.
- AO: `ambientOcclusion` defaults to false; `aoRadius` is (0, 10000], `aoSamples` is 1–32 (default 4), `aoStrength` is 0–4 (default 1). Finite-distance cosine-weighted rays affect ambient lighting only.
- Reflections: `reflections` defaults to false; `reflectionDistance` is (0, 100000], `reflectionSamples` is 1–16 (default 1), `reflectionStrength` is 0–1 (default 1). One-bounce GGX rays shade nearest hits and use the environment on misses, replacing environment-only specular. Set `halfResolutionReflections` to true to trace one representative pixel per 2×2 block and reconstruct the remaining pixels using scene depth and normals. Pixels without a compatible neighbor trace at full resolution. The setting defaults to false on both C++ and Web and affects reflections only.
- Diffuse bounce: `diffuseBounce` defaults to false. Four cosine-weighted rays per opaque PBR pixel gather one bounce of shaded surface radiance within ten scene units. Misses leave the existing environment lighting in place. This adds local color spill, with no probe cache.
- DDGI probes: `probeGI` defaults to false. Metal, Vulkan and WebGPU update up to eight probes per frame in a 5 × 4 × 5 grid fitted to scene bounds. Deferred lighting blends neighboring probes using directional hit-distance moments to reduce leaking. With probes on, the per-pixel bounce is bypassed. A scene, material or lighting change restarts the probe cache.
- History: `temporalReconstruction` defaults to false; `historyWeight` is 0–0.98 (default 0.9). Reprojection uses depth, normal and instance rejection, neighborhood clamping and motion-dependent weighting before post processing and final AA. Transparent OIT overlays are composited afterward.

`view` selects `Final`, `Shadows`, `AmbientOcclusion` or `Reflections` in C++; Web uses `'final'`, `'shadows'`, `'ambient-occlusion'`, `'reflections'`. Diagnostics bypass AA/post. `shadowLight` selects active-light index 0–15; unused slots are fully visible.

## History and limits

`GetHybridHistoryFrames()` / `getHybridHistoryFrames()` reports history frames. `ResetHybridHistory()` / `resetHybridHistory()` clears them. Smooth camera movement preserves history; cuts (>20° or >2 world units), lens/exposure, scene/material/light, viewport and size changes reset it.

Hybrid selects Deferred. Off, FXAA, SMAA and SSAA work; MSAA, TAA and AA split are rejected. Static instances and MASK coverage are supported. BLEND remains OIT, without shadow casting or ray-traced glass. Secondary reflection hits use material textures, including normal maps and material AO. The tangent frame comes from the hit triangle and its UVs, with geometric shading for degenerate UVs. Reflections use one bounce.

Animated geometry, moving-instance motion vectors and hardware ray queries remain unsupported. The DDGI grid uses six lobes per probe and one bounce. It has no relocation, adaptive placement or recursive bounce, and DX12 does not expose probe mode. Scene edits conservatively reset history. Reconstruction uses combined color, without per-effect variance-guided spatial filtering.
