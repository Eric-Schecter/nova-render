# Shadows

`ShadowView` is a procedural directional shadow-map laboratory with no model dependency and shared native/WebGPU HLSL.

```cpp
#include <render/ShadowView.hpp>
render::ShadowView view;
auto shadows = view.GetShadowSettings();
shadows.method = render::ShadowMethod::PCSS;
view.SetShadowSettings(shadows);
window.PresentImage(view.DrawFrame(800, 485));
```

```js
const view = await Nova.createShadowView({ canvas })
view.setShadowSettings({ method: 'pcss', lightSize: 0.35 })
await view.drawFrame(800, 485)
```

- `method`: Off / Hard / PCF / PCSS, lowercase on web, default PCSS.
- `resolution`: 512 / 1024 / 2048, default 1024.
- `depthBias`: 0–0.02, default 0.001; excessive bias detaches shadows.
- `normalBias`: 0–0.1, default 0.015; offsets along the normal, more on slopes.
- `filterRadius`: 1–6 shadow texels, default 2, PCF only.
- `lightSize`: 0.01–1.5 scene units, default 0.35, PCSS only.
- `lightDirection`: finite nonzero surface-to-light vector, default [-1.5,3,2].

Use `SetShadowSettings` / `GetShadowSettings`, AA/camera get/set, time, reset/history and drawing. Web names start lowercase and setters allow partial fields. Invalid settings preserve state.

Default AA is TAA; all six modes are available. Shadow changes reset history; small camera/object movement reprojects motion. Camera distance 4–20, pitch ±60°, output dimensions 1–2048; the example caps the long side at 1024 without automatic quality reduction.

Hard uses one comparison. PCF filters 32 disk comparisons. PCSS uses 24 blocker-search samples then 32 penumbra samples based on blocker/receiver distance. This is a fixed-coverage single shadow map, not ray tracing; no cascades, point lights or transmissive shadows.

Currently limited to the built-in test scene, not arbitrary GLB Renderer models. Native Image readback and all passes/waits are included in metrics.
