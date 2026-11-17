# Raster pipeline laboratory

This API is available in the current source checkout; the published 0.7.1 packages predate it.

`PipelineComparisonView` / `createPipelineComparisonView` renders a shared procedural scene. These pipeline choices belong to this independent view; global `RenderMode` remains Raster / RayTracing.

```cpp
#include <render/PipelineComparisonView.hpp>
render::PipelineComparisonView view;
render::PipelineComparisonSettings settings;
settings.pipeline = render::RasterPipeline::Deferred;
settings.debug = render::PipelineDebug::WorldNormal;
settings.lightCount = 128;
view.SetSettings(settings);
const render::Image& image = view.DrawFrame(800, 485);
// window.PresentImage(image); window.DrawFrame();
```

```js
import { createPipelineComparisonView } from '@nova/render-web'
const view = await createPipelineComparisonView({ canvas, shaderDirectory })
view.setSettings({ pipeline: 'deferred', debug: 'world-normal', lightCount: 128 })
await view.drawFrame(800, 485)
// view.dispose() when the application closes the viewport.
```

C++ methods are `SetSettings`, `GetSettings`, `SetCamera`, `GetCamera`, `DrawFrame`. Web uses lower camel case and partial settings updates; C++ settings replace the entire value. Camera uses `OrbitCamera`, degrees, distance 4–50 and pitch ±80°. Target is supported; lens and display exposure are fixed for this experiment. Dimensions must be integers from 16 × 9 through 2048 × 2048. Native frames are synchronous RGBA8 images; Web presents to the supplied canvas and awaits GPU completion. Web also exposes `readPixels()` returning RGBA16F bytes, width, height and padded row stride.

Settings:

- `pipeline`: C++ `Forward`, `Deferred`, `ForwardPlus`, `ClusteredForward`; Web `forward`, `deferred`, `forward-plus`, `clustered-forward`.
- `debug`: C++ `Final`, `BaseColor`, `WorldNormal`, `LinearDepth`, `Roughness`, `Metallic`, `LightCount`, `Overflow`; Web `final`, `base-color`, `world-normal`, `linear-depth`, `roughness`, `metallic`, `light-count`, `overflow`.
- `lightCount`: integer 0–256, default 128.
- `lightRadius`: (0, 32], default 3. Light influence is exactly zero beyond this radius.
- `lightIntensity`: [0, 100], default 18.

The deferred G-buffer uses four RGBA16F targets: linear base color; signed world-space normal; world position with positive linear view depth in alpha (zero means background); roughness and metallic. Base color is unlit and displayed numerically without a lighting or tone-mapping transform. World normal maps −1…1 to 0…1. Depth maps 0…64 to 0…1. These are diagnostic views, not shaded previews.

Forward+ reduces visible depth per tile in a 64-thread Compute Shader workgroup; empty tiles receive no lights. Clustered Forward uses fixed near/far planes 0.1/64. Conservative sphere–view-cell AABB intersection can include extra lights but preserves all contributing lights. The compact atlas stores a count and up to 64 one-based light indices per cell; The count/index atlas and tile depth ranges use RGBA32F storage textures; the G-buffer and displayed-frame readback remain RGBA16F. Overflow falls back to the all-light loop. The heat map shows evaluated candidates divided by the scene light count, not just lights that contribute nonzero radiance. Forward and Deferred evaluate all lights.

Performance bars measure completed draws, including host preparation and GPU waits. Native timings additionally include image readback/presentation; they are not pure GPU timestamps and should not be used for absolute cross-platform comparisons. Continuous benchmark repeatedly redraws an otherwise static scene; it pauses while the Web example is hidden. No environment assets or external scene files are required.

Both culling paths dispatch one 64-thread Compute Shader workgroup per cell: 16 × 9 × 1 for Forward+, 16 × 9 × 16 for Clustered Forward. Threads test disjoint light IDs into shared memory; lane zero performs a bounded stable compaction of at most 256 flags. Ascending light IDs preserve the all-light summation order. Every active cell writes its count and all 64 index slots, including empty cells, to prevent stale entries after setting changes. Native backends issue storage-write to sampled-read transitions; WebGPU tracks the compute/render-pass usages. Depth reduction uses a shared-memory tree, without subgroup features or storage atomics. The same `PipelineCSMain` HLSL entry point compiles for DXIL, SPIR-V, Metal and WGSL; no runtime shader compiler or additional dependency is required.
