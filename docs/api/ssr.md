# Screen-space reflections

`ScreenSpaceReflectionView` encapsulates example 12, using shared HLSL on Metal, Vulkan, DX12 and WebGPU without new dependencies.

```cpp
#include <render/ScreenSpaceReflectionView.hpp>
render::ScreenSpaceReflectionView view;
auto settings = view.GetScreenSpaceReflections();
settings.hierarchical = true;
view.SetScreenSpaceReflections(settings);
window.PresentImage(view.DrawFrame(800, 485));
```

```js
const view = await Nova.createScreenSpaceReflectionView({ canvas })
view.setScreenSpaceReflections({ hierarchical: true, maxSteps: 128 })
await view.drawFrame(800, 485)
```

The raster surface pass produces HDR color, motion vectors, world normals/roughness and linear view depth. A complete conservative min/max depth hierarchy covers NPOT sizes down to 1×1. Screen-space DDA skips nonintersecting cells, descends and refines hits at the finest level.

Hit color uses a roughness-dependent nine-tap filter with depth-aware rejection, not multiple stochastic GGX rays. Misses, offscreen rays, high roughness and exhausted budgets fall back to prefiltered HDR environment reflections. Edges, distance and roughness thresholds blend smoothly; disabling SSR still preserves environment reflection.

- `enabled`: default true.
- `hierarchical`: default true; false selects linear screen-space stepping.
- `maxSteps`: 16–256, default 128, including descent/advance iterations.
- `maxDistance`: 1–60 scene units, default 24.
- `thickness`: 0.01–1 view-depth tolerance, default 0.12; excessive values leak through thin surfaces.
- `stride`: 0.5–4 screen pixels, default 1, linear mode only.
- `roughnessCutoff`: 0.05–1, default 0.8.
- `edgeFade`: 0.005–0.3 normalized border width, default 0.08.
- `intensity`: 0–2, default 1.
- `environmentIntensity`: 0–4, default 1.
- `debug`: Composite / Reflections / HitMask / Normals / Roughness / LinearDepth / DepthPyramid; web strings are `composite`, `reflections`, `hit-mask`, `normals`, `roughness`, `linear-depth`, `depth-pyramid`.
- `debugMip`: 0–12, default 3; zero is full-resolution depth; unavailable levels clamp to the coarsest.

`SetScreenSpaceReflections` / `GetScreenSpaceReflections` correspond to lowercase web methods; web setters accept partial fields. Invalid settings preserve the old values. `LoadEnvironment(path)` / `loadEnvironment(url)` loads `.nenv` / web `.bin`; `UseDefaultEnvironment()` / `useDefaultEnvironment()` restores the procedural HDR studio.

Camera/time, six AA modes, post-processing and reset methods match the other labs. SSR composites before HDR TAA/post. Motion follows real transforms; TAA rejects history on large hit-confidence changes. Data debug views bypass tone mapping/bloom.

MSAA resolves actual four-sample surfaces; SSAA traces at doubled internal dimensions then downsamples. Output is 1–2048; the example defaults to 75% scale with long side capped at 1024. Metrics include surfaces, hierarchy, tracing, AA, post and waits, plus native readback.

This is an internal scene, not arbitrary GLB SSR. Only the first currently visible surface is available; environment fallback is not an accurate reflection of hidden/offscreen objects.
