# Ambient occlusion

AO is an independent effect, not a new render mode. Example 8 uses an untextured white-model diagnostic to compare SSAO, HBAO, MSAO and RTAO without material textures or baked AO.

```cpp
#include <render/RayTracingView.hpp>
render::RayTracingView view;
view.LoadModel("assets/SponzaWhite/SponzaWhite.glb");
render::AmbientOcclusionSettings ao;
ao.enabled = true;
ao.method = render::AOMethod::HBAO;
view.SetAmbientOcclusion(ao);
window.PresentImage(view.DrawFrame(640, 480));
```

```js
await renderer.loadModel('/generated/SponzaWhite.glb')
renderer.setAmbientOcclusion({ enabled: true, method: 'hbao' })
await renderer.drawFrame()
```

`SetAmbientOcclusion` / `GetAmbientOcclusion` correspond to lower-camel-case web methods.

- `enabled`: default false; enabled output is the diagnostic white model. `method`: Disabled / SSAO / HBAO / MSAO / RTAO, lowercase strings on web. Disabled displays the white model without AO, not a different Mode.
- `output`: Composite / Occlusion / Depth / Normals, lowercase on web. Composite modulates ambient light while preserving fixed directional shading; it is not a complete AO post-process added to ordinary PBR materials.
- `radius`: 0.001–1, default 0.08 in normalized model coordinates; `bias`: 0–0.05, default 0.001; `strength`: 0–4, default 1.
- `samples`: 4–64, default 16. Direction/step grouping and multiscale sampling mean equal counts do not imply equal costs.
- `denoise`: default true, depth/normal-guided display filtering. `resolutionScale`: 0.125–1, default 0.5. `seed`: uint32, default 1.

SSAO projects hemisphere samples into depth; HBAO searches screen-space horizons; MSAO combines original and 2×/4×/8× minimum-depth levels; RTAO casts finite-radius cosine-hemisphere visibility rays. These are Nova implementations, not complete vendor SDK ports.

Shared HLSL generates primary-ray depth and normals for consistent native/web inputs. The first three algorithms still estimate occlusion only from screen depth; hidden/offscreen surfaces are unavailable. Native RTAO uses ray queries, web uses compute BVH. The current native diagnostic depends on backend ray-tracing support and is not an optimized raster G-buffer AO path.

`GetAccumulatedSamples()` / `getAccumulatedSamples()` reports frames in this view. Each RTAO frame casts `samples` rays per pixel; the example stops at 32 frames, while screen-space methods draw on demand. Camera, size and AO changes reset accumulation. Display filtering never feeds back into raw accumulation.

Indoor cameras accept a finite three-component `OrbitCamera.target` / `camera.target`, default [0,0,0], with distance 1.5–100. Example presets and stopping policy belong to the application.

Example-level `getPerformanceStats()` includes completed-update FPS, Draw/P95 time, internal dimensions and state. `setAdaptiveQuality(false)` disables temporary interaction quality reduction. P95 excludes the first resource-preparation draw and uses the last 60 draws. Draw includes CPU preparation/GPU waits, not pure GPU time. Interaction preview restores user resolution and samples once movement stops.
