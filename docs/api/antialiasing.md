# Anti-aliasing

Ordinary raster drawing in Renderer and Window supports triangles, models, lighting, skies and GPU skinning with six AA modes, default Off. AO diagnostics and ray tracing keep their own sampling pipelines; AA is not a new Mode.

```cpp
render::AntiAliasingSettings aa;
aa.method = render::AAMethod::TAA;
window.SetAntiAliasing(aa);
window.DrawFrame();
```

```js
renderer.setAntiAliasing({ method: 'taa' })
await renderer.drawFrame()
```

`GetAntiAliasingHistoryFrames()` / `getAntiAliasingHistoryFrames()` reads TAA history. Small camera changes and skinning reproject previous matrices/joint poses. Models, environments, exposure, algorithm, size and large camera changes reset history. Static web previews settle for 32 frames; animation keeps rendering.

Model textures generate full mip chains, trilinear filtering and up to 8× anisotropy (subject to Vulkan device support). Base color/emission use sRGB/linear conversion; normals, material and occlusion data remain linear.

## Independent laboratory

`AntiAliasingView` adds split comparison, zoom, time and procedural checker filtering:

```cpp
#include <render/AntiAliasingView.hpp>
render::AntiAliasingView view;
render::AntiAliasingSettings settings;
settings.method = render::AAMethod::TAA;
view.SetAntiAliasing(settings);
view.SetTime(0.1f);
window.PresentImage(view.DrawFrame(800, 485));
```

```js
const view = await Nova.createAntiAliasingView({ canvas, shaderDirectory: '/generated/' })
view.setAntiAliasing({ method: 'taa', split: true })
view.setTime(0.1)
await view.drawFrame(800, 485)
```

Both versions provide AA get/set, camera get/set, time, reset/history and draw methods. Web names use lower camel case, partial setting objects and `dispose()`; native setters receive complete structs and RAII releases resources.

- `textureFiltering`: default true; screen-derivative analytic checker filtering. This switch does not disable general model texture filtering.
- `method`: Off / FXAA / SMAA / MSAA / SSAA / TAA, lowercase on web; laboratory default TAA.
- `historyWeight`: 0–0.98, default 0.9, TAA only. Higher values rely more on history and may soften motion.
- `zoom`: 1–8, default 1, centered image magnification.
- `split`: default true; left is the unjittered Off reference at the same time, right uses selected AA.

Camera defaults: yaw 20°, pitch 15°, distance 6.2, exposure 1. Distance is 4–20, pitch ±60°, exposure (0,4]. Time is 0–1,000,000 seconds. Output dimensions are integer 1–2048; examples cap the long side at 1024. SSAA doubles internal width and height.

## Algorithms

- **Off**: single-sample raster reference.
- **FXAA**: NVIDIA FXAA 3.11, quality 29; gamma-luminance alpha detects edges while color blending remains linear.
- **SMAA 1x**: reference HIGH preset; edge detection → Area/Search weights → neighborhood blending. No T2x/S2x/4x variants.
- **MSAA 4x**: genuine four-sample color/depth and hardware resolve. It handles geometric coverage, not all high-frequency shader aliasing.
- **SSAA 2×2**: four full shading samples per output pixel, downsampled in linear color, with higher shading/storage cost.
- **TAA**: eight-phase Halton jitter, transform-derived motion, depth/material rejection and 3×3 neighborhood clamping. Parameter changes, size changes, time jumps and camera cuts reset history. No motion blur, sharpening or upscaling.

`GetHistoryFrames()` returns zero outside TAA. Example scheduling, hidden-view pause and stopping budgets are application policy. Native AA reads offscreen results back into Image before presentation; web displays directly. Draw/FPS include all work and waits, not pure GPU timing. Quality is not automatically reduced.
