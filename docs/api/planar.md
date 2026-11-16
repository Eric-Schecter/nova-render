# Planar reflections

`PlanarReflectionView` encapsulates example 13 with shared native/web HLSL and no new dependencies.

```cpp
#include <render/PlanarReflectionView.hpp>
render::PlanarReflectionView view;
auto reflections = view.GetPlanarReflections();
reflections.floorRoughness = 0.1f;
reflections.mirrorIntensity = 0.8f;
view.SetPlanarReflections(reflections);
window.PresentImage(view.DrawFrame(800, 485));
```

```js
const view = await Nova.createPlanarReflectionView({ canvas })
view.setPlanarReflections({ floorRoughness: 0.1, mirrorIntensity: 0.8 })
await view.drawFrame(800, 485)
```

`SetPlanarReflections` / `GetPlanarReflections` correspond to `setPlanarReflections` / `getPlanarReflections`; web setters accept partial fields.

- `floorEnabled` / `mirrorEnabled`: default true; disabled planar views use environment reflections.
- `floorRoughness` / `mirrorRoughness`: 0–1, defaults 0.06 / 0.02.
- `floorIntensity` / `mirrorIntensity`: 0–2, default 1; zero removes reflection contribution.

Invalid values preserve settings. Camera/time/environment, six AA modes, post-processing and history methods match [SSR](./ssr).

Each enabled plane draws an extra reflected scene view, equivalent to a mirrored camera. Lighting keeps original world positions; geometry behind the clip plane is rejected. HDR skies fill the reflected background. This can show objects outside the main view and composites only on receivers, before AA/post. TAA uses reflected-object motion rather than receiver motion, though rough/multilayer motion can still leave approximate history artifacts.

Single bounce only: reflected views omit both receivers to avoid recursion. Fixed floor y=-1 and mirror z=-3.2; no arbitrary scene/custom planes yet. View from above the floor/front of the mirror. Roughness uses a 25-tap Gaussian approximation, not microfacet multi-ray integration, so high-roughness and occlusion boundaries remain approximate.

Reflection views match internal scene size. SSAA doubles both axes; MSAA 4x applies to the main scene while reflection views are single-sampled. Metrics include reflected views, composite, AA, post, waits and native readback.
