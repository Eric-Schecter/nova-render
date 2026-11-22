# Model raster pipelines

Available in SDK 0.8.0. `Renderer` and `Window` expose the same C++ settings; the browser renderer uses matching lower-camel-case methods.

```cpp
render::Window window;
window.LoadModel("DamagedHelmet.glb");
window.SetLighting(render::LightingMode::PBR);
render::RasterSettings settings;
settings.pipeline = render::RasterPipeline::Deferred;
window.SetRasterSettings(settings);
// Inspect a real GPU G-buffer target:
settings.view = render::GBufferView::WorldNormal;
window.SetRasterSettings(settings);
// PollEvents / DrawFrame as usual.
```

```js
import { createRenderer } from '@nova/render-web'
const renderer = await createRenderer({ canvas, shaderDirectory: '/nova/' })
await renderer.loadModel('/DamagedHelmet.glb')
renderer.setLighting('pbr')
renderer.setRasterSettings({ pipeline: 'deferred' })
renderer.setRasterSettings({ view: 'world-normal' })
await renderer.drawFrame()
// renderer.dispose() on teardown.
```

`SetRasterSettings` replaces all C++ fields; `setRasterSettings` merges partial Web settings. `GetRasterSettings` / `getRasterSettings` return the selected settings. Pipeline/view changes reset raster history. `GetRasterCapabilities` / `getRasterCapabilities` report supported pipelines and restrictions. `RenderMode` remains Raster / RayTracing; selecting a raster pipeline does not create another rendering mode. Raster settings are retained while ray tracing is active.

Settings:

- `pipeline`: `RasterPipeline::Forward` / `'forward'` (default), or `Deferred` / `'deferred'`. `ForwardPlus` and `ClusteredForward` remain available only in the separate [pipeline laboratory](pipelines); the model setter rejects them.
- `view`: `GBufferView::Final` / `'final'`, `BaseColor` / `'base-color'`, `WorldNormal` / `'world-normal'`, `GeometricNormal` / `'geometric-normal'`, `LinearDepth` / `'linear-depth'`, `Roughness` / `'roughness'`, `Metallic` / `'metallic'`, `Occlusion` / `'occlusion'`, `Emissive` / `'emissive'`, `WorldPosition` / `'world-position'`. Diagnostics require Deferred.
- `debugRange`: positive finite distance ≤ 1,000,000, default 20. Depth maps 0…range to 0…1; world position maps −range…range to 0…1 per component. Normals map −1…1 to 0…1. Scalar/color diagnostic values are displayed numerically, without lighting, exposure or tone mapping; values outside 0…1 are clamped.

Deferred and Hybrid use seven RGBA16F color targets and a separate hardware depth attachment. `GetRasterCapabilities()` and `getRasterCapabilities()` report `gBufferTargets: 7` and `gBufferBytesPerPixel: 56`. WebGPU requires 56 color bytes per sample and 22 sampled textures per shader stage. Half-resolution Hybrid reflections use a 23rd sampled texture. WebGPU scenes with sheen select Deferred and report `materialDeferredRequired: true` in the raster capabilities. Deferred accepts a loaded model or scene, including an empty scene.

The normal frame path retains the selected AA and post-processing. Deferred supports Off, FXAA, SMAA, SSAA and TAA. Either setting Deferred while MSAA is selected, or selecting MSAA while Deferred is active, raises an error without changing settings. The panel disables those incompatible options. Diagnostics temporarily bypass AA/post, preserving the requested values for Final. Their background is black. Forward uses the original direct path when neither AA nor post-processing is enabled.

## Shared GPU surface layout

All surface sampling and PBR lighting live in `model.hlsl`. `GBufferPSMain` samples the same material factors, atlas, normal maps and GPU-skinned/instanced vertices used by `PSMain`. `DeferredPSMain` decodes the surface and calls the shared lighting function. Geometry is depth tested; full-screen lighting runs once per covered pixel. Empty pixels retain the configured clear color or sky background. Sky gradients are evaluated before coverage branching and sampled with explicit gradients for WebGPU derivative uniformity.

The G-buffer stores seven RGBA16F targets.

- Base RGB (linear), roughness A. Roughness is clamped to ≥ 0.06; zero identifies background.
- Two octahedral normal pairs: XY = world-space material normal after normal mapping; ZW = unmapped vertex normal used for shadow bias. Both are signed octahedral coordinates.
- Split logarithmic view depth in XY, material occlusion Z, relative material slot W. A half-representable high part and a residual preserve depth precision; camera rays reconstruct world position for both perspective and orthographic views without an extra target. This replaces the original half-float camera-relative XYZ encoding in current source.
- Emissive RGB (linear HDR), metallic A.
- Octahedral clearcoat normal, clearcoat strength and roughness.
- Specular and sheen texture modulation.
- Iridescence texture modulation, anisotropy tangent angle and strength.

These targets remain GPU-resident between geometry and lighting. Native paths reuse the raster device and encode geometry/lighting in the same AA command sequence with resource transitions; Vulkan keeps material bindings and G-buffer bindings in separate descriptor sets. WebGPU uses two corresponding bind groups. Consumers do not manage backend image handles. Metal/DX12 AA retain their own command queue and synchronize completion before presentation. Raster retains the existing image bridge; Hybrid directly samples the owned GPU output texture and keeps readback explicit.

Runtime material-slot changes, texture replacement, multi-instance transforms, perspective/orthographic cameras, viewport rectangles, GPU skinning, directional/point/spot lights, existing scene shadows, environment lighting, fog and post-processing reuse the formal model APIs. Shadows retain their existing scene requirement and algorithm; Deferred does not introduce another shadow implementation.

## Hybrid preparation status

This adds raster primary visibility and deferred composition, not a Hybrid mode. TAA motion remains a separate existing pass. Instance identity, complete previous-instance transforms, effect histories/disocclusion, GPU sharing with the ray backends, secondary-ray effects and DDGI are still required. See the [hybrid plan](../development/hybrid-mode-plan) and [validation](../validation/deferred).
