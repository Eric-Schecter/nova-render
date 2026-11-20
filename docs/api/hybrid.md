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
- Enable `halfResolutionAO` with `ambientOcclusion` to trace one representative per 2×2 block, accumulate a separate AO history, and reconstruct with depth and geometric normals. The AO diagnostic continues to show raw full-resolution visibility. This option defaults to false.
- Enable `blueNoiseAO` with half-resolution AO to rotate the hemisphere ray set using a 32 × 32 spatial blue-noise rank tile and a low-discrepancy temporal rotation. It defaults to false.
- Enable `shadowTemporalReconstruction` to accumulate ray-traced light visibility before deferred lighting. It defaults to false in C++ and Web, works independently of full-frame reconstruction, and rejects history using scene motion, depth, normals, and instance identity. This covers directional, point and spot lights. Rectangle lights integrate visibility during emitter sampling. The Shadows debug view displays raw visibility.
- Reflections: `reflections` defaults to false; `reflectionDistance` is (0, 100000], `reflectionSamples` is 1–16 (default 1), `reflectionStrength` is 0–1 (default 1). One-bounce GGX rays shade nearest hits and use the environment on misses, replacing environment-only specular. With `clearcoatReflections` enabled (the default), coated pixels trace a second lobe using their clearcoat normal and roughness, including both maps when present. The coat attenuates the base layer and the traced contributions share the half-resolution HDR buffer and history. Turn the setting off to compare with environment-only clearcoat. Set `halfResolutionReflections` to true to trace one representative pixel per 2×2 block and reconstruct the remaining pixels using scene depth and normals. Pixels without a compatible neighbor trace at full resolution. The setting defaults to false on both C++ and Web and affects reflections only. Enable `reflectionDenoising` to reproject a separate half-resolution reflection history using surface motion, depth, normals, and instance identity. The filter rejects incompatible history and limits isolated bright samples on rough surfaces. It works with or without full-frame `temporalReconstruction` and leaves AO sampling unchanged. Both C++ and Web default to false for direct comparison.
- Enable `mirrorReflections` to trace one deterministic mirror ray per lobe at roughness 0.05 or below. Half-resolution reflection denoising skips pixels whose traced lobes all qualify. The shortcut defaults to false.
- Enable `reflectionHitReprojection` with half-resolution reflection denoising to use secondary hit distance for temporal reprojection on smooth planar surfaces. It defaults to false on C++ and Web. Rough, curved and coated surfaces with two traced lobes, unstable hit distances, sky misses and orthographic cameras use surface motion.
- Diffuse bounce: `diffuseBounce` defaults to false. Four cosine-weighted rays per opaque PBR pixel gather one bounce of shaded surface radiance within ten scene units. Misses leave the existing environment lighting in place. This adds local color spill, with no probe cache.
- DDGI probes: `probeGI` defaults to false. Metal, Vulkan and WebGPU update up to eight probes per frame in a 5 × 4 × 5 grid fitted to scene bounds. Deferred lighting blends neighboring probes using directional hit-distance moments to reduce leaking. With probes on, the per-pixel bounce is bypassed. A scene, material or lighting change restarts the probe cache.
- Enable `probeSkyLighting` with `probeGI` to sample the loaded environment on probe-ray misses. Valid probe coverage blends in visibility-aware diffuse lighting and blends out diffuse IBL. Specular IBL remains active. The option defaults to false and changing it rebuilds the probe cache.
- Enable `probeMultiBounce` with DDGI sky lighting and a loaded environment to feed the previous probe cache into diffuse shading at probe-ray hits. It defaults to false. Updates read a stable snapshot, reuse depth visibility, and apply 65% feedback to limit energy buildup. Changing the option rebuilds the cache.
- Enable `probeReflectionLighting` with DDGI sky lighting and a loaded environment to replace diffuse IBL at reflection hits with local probe lighting. This applies to full- and half-resolution Hybrid reflections and defaults to false.
- Enable `probeSharedRays` to trace 96 spherical rays per probe and reuse their results across all six directional lobes in workgroup memory. It defaults to false. Changing it restarts the cache.
- Enable `probeRoughReflections` with DDGI sky lighting to approximate reflection lobes of roughness 0.8 or greater using cached radiance. This skips tracing only with full probe coverage and skips denoising only when every lobe is already filtered or a deterministic mirror. It defaults to false.
- History: `temporalReconstruction` defaults to false; `historyWeight` is 0–0.98 (default 0.9). Reprojection uses depth, normal and instance rejection, neighborhood clamping and motion-dependent weighting before post processing and final AA. Transparent OIT overlays are composited afterward.

`view` selects `Final`, `Shadows`, `AmbientOcclusion` or `Reflections` in C++; Web uses `'final'`, `'shadows'`, `'ambient-occlusion'`, `'reflections'`. Diagnostics bypass AA/post. `shadowLight` selects active-light index 0–15; unused slots are fully visible.

## History and limits

`GetHybridHistoryFrames()` / `getHybridHistoryFrames()` reports history frames. `ResetHybridHistory()` / `resetHybridHistory()` clears them. Smooth camera movement preserves history; cuts (>20° or >2 world units), lens/exposure, scene/material/light, viewport and size changes reset it.

Hybrid selects Deferred. Off, FXAA, SMAA and SSAA work; MSAA, TAA and AA split are rejected. Static instances and MASK coverage are supported. BLEND remains OIT, without shadow casting or ray-traced glass. Secondary reflection hits use material textures, including normal maps and material AO. The tangent frame comes from the hit triangle and its UVs, with geometric shading for degenerate UVs. Reflections use one bounce.

Animated geometry, moving-instance motion vectors and hardware ray queries remain unsupported. The DDGI grid uses six lobes per probe. It has no relocation or adaptive placement, and DX12 does not expose probe mode. Scene edits conservatively reset history. Full-frame reconstruction uses combined color. Independent shadow reconstruction filters visibility in time. Half-resolution reflections also use a spatial filter.


### Local probe bounds

When DDGI is enabled, set `HybridSettings::probeBounds` in C++ or `probeBounds` in JavaScript to place the probe grid in a world-space box. Specify finite, ordered minimum and maximum coordinates on all three axes. For example, the CarConcept showroom uses `{ minimum: [-2.65, -0.12, -3.25], maximum: [2.65, 1.95, 3.25] }`. Leave the option unset in C++ or use `null` in JavaScript to derive the box from scene geometry. Changing the box resets the probe cache.
