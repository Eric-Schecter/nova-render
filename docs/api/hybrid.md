# Hybrid rendering

Available in the current source; published 0.8.0 packages predate this mode. Use matched headers, libraries and shader assets from one build.

C++: `SetRenderMode(RenderMode::Hybrid)`, `SetHybrid(settings)`, `GetHybrid()`, `GetHybridCapabilities()` on Renderer / Window. Web: `setRenderMode('hybrid')`, `setHybrid(settings)`, `getHybrid()`, `getHybridCapabilities()`.

The first implementation uses raster primary visibility, a four-target material G-buffer, a separate per-light visibility pass and deferred PBR composition. Native frames sample GPU output directly; explicit validation captures can read back separately. The visibility pass runs the shared HLSL BVH in fragment shaders and reports `execution: 'fragment-bvh'`, `softwareRayTracing: true`, `hardwareRayTracing: false`. This is not yet shared hardware-query traversal or asynchronous frames in flight.

```cpp
render::HybridSettings hybrid;
hybrid.shadows = true;
hybrid.shadowSamples = 4;
window.SetHybrid(hybrid);
window.SetRenderMode(render::RenderMode::Hybrid);
```

```js
renderer.setHybrid({ shadows: true, shadowSamples: 4 })
renderer.setRenderMode('hybrid')
```

Settings:

- `shadows`: default true; Hybrid's shadow master. Per-light `castShadow`, softness and instance shadow flags remain effective. Raster's separate shadow master does not disable Hybrid shadows.
- `shadowSamples`: integer 1–16, default 4. A zero-softness light uses one hard visibility ray. Soft lights use the requested deterministic disk samples and the existing softness units; there is no temporal accumulation yet.
- `view`: C++ `HybridView::Final / Shadows`, Web `'final' / 'shadows'`. Shadows displays visibility for one active light and bypasses AA/post processing.
- `shadowLight`: integer 0–15, default 0; index in the enabled-light collection. Without a collection, index 0 is the legacy directional light. Unused slots are fully visible.

Capabilities report `shadows`, `rasterPrimaryVisibility`, `gpuPresentation`, `alphaMask` and `alphaBlend` as supported. Static models and instances are supported; animated geometry is rejected. MASK uses alpha-tested visibility. BLEND remains a raster OIT overlay and casts no shadow; it does not become ray-traced glass.

AO, reflections, global illumination and temporal reconstruction are currently false. MSAA, TAA and AA split are rejected; Off, FXAA, SMAA and SSAA are supported. Entering Hybrid selects Deferred; switching the raster pipeline to Forward while Hybrid is active is rejected. Settings validation and incompatible mode changes preserve the last valid state.
