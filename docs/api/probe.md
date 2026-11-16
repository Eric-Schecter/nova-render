# Reflection probes

`ReflectionProbeView` encapsulates example 14 with shared C++/WebGPU HLSL and no new dependencies.

```cpp
#include <render/ReflectionProbeView.hpp>
render::ReflectionProbeView view;
auto probe = view.GetReflectionProbe();
probe.boxProjection = true;
probe.position = {1, 0.6f, 0};
view.SetReflectionProbe(probe);
window.PresentImage(view.DrawFrame(800, 485));
```

```js
const view = await Nova.createReflectionProbeView({ canvas })
view.setReflectionProbe({ boxProjection: true, position: [1, 0.6, 0] })
await view.drawFrame(800, 485)
```

`SetReflectionProbe` / `GetReflectionProbe` map to lowercase web methods. Web setters accept partial objects and getters copy arrays.

- `enabled`: local cubemap on/off, default true; off uses the global environment.
- `boxProjection`: default true.
- `roughness`: sphere roughness, 0–1, default 0.12.
- `intensity`: global reflection intensity, 0–2, default 1.
- `position`: legacy single-probe world position, default [0,0.6,0].
- `boxMin` / `boxMax`: legacy projection/influence box, defaults [-4,-1,-4] / [4,3,4]. Coordinates are within ±20, each extent at least 0.25, with the probe strictly inside the projection box.
- `blendDistance`: legacy inside-border fade, 0–2, default 0. Outside uses environment reflection.
- `debug`: C++ `ProbeDebug::Composite / Reflections / Influence / Cubemap`; web lowercase strings. A single probe uses green/local and red/environment; multiple probes use red/green/blue/yellow normalized contributions, gray for environment.

## Multiple probes

`SetReflectionProbes(volumes)` / `setReflectionProbes(volumes)` sets up to four `ReflectionProbeVolume` entries; getters return copies. `ReflectionProbeSettings.probes` is equivalent. An empty list restores legacy single-probe fields; the example creates three probes.

Each volume has `enabled`, `position`, `boxMin`/`boxMax` for box projection, `influenceMin`/`influenceMax` for weighting, `intensity` and `blendDistance`. Separate projection/influence boxes allow overlapping regions without moving the room's proxy walls. Defaults match the single projection box; default influence is also [-4,-1,-4] / [4,3,4], intensity 1 and blend distance 0.8.

```cpp
render::ReflectionProbeVolume left, right;
left.position = {-2, 0.6f, 0};
right.position = {2, 0.6f, 0};
left.influenceMax[0] = 1;
right.influenceMin[0] = -1;
view.SetReflectionProbes({left, right});
```

```js
view.setReflectionProbes([
  { position: [-2, 0.6, 0], influenceMax: [1, 3, 4] },
  { position: [2, 0.6, 0], influenceMin: [-1, -1, -4] },
])
```

`smoothBlending` defaults true. Each volume fades inside its boundary with smoothstep; overlap weights are normalized to avoid increased brightness. Missing coverage uses the environment. Disabling smoothing shows hard transitions. Per-probe intensity applies before global intensity. `debugProbe`, default zero, selects the cubemap; invalid indices are rejected.

Moving a probe rebuilds only that cubemap. Environment changes or `UpdateProbe()` / `updateProbe()` invalidate all; the indexed overload refreshes one. Camera, size, AA, roughness, intensity and box edits reuse captures. `GetProbeCaptureCount()` / `getProbeCaptureCount()` counts completed captures. Invalid settings preserve state.

## Capture and limits

The static procedural room/pillars are captured by shared HLSL analytic intersection along six 90° views, with 64 GGX importance samples for roughness levels. Logical cubemap faces use a 2D atlas, 128×128 per face and eight roughness levels. Metadata uses a separate half-float texture. This is not yet arbitrary GLB capture; spheres and position markers are omitted to avoid self/recursive reflections.

Box projection intersects reflection rays with the proxy box, then samples from the probe toward that hit. It improves wall parallax but cannot reconstruct exact prop parallax, occlusion or sphere-to-sphere reflections. Four axis-aligned probes maximum; no rotated boxes. Rough reflections remain prefilter approximations.

Camera/time/environment, six AA modes, post-processing and history controls match [SSR](./ssr). Performance includes capture only when needed, main rendering, composite, AA, post and waits; native timing also includes Image readback.
