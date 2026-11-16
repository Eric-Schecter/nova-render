# Scene instances

Available on `main` for the next SDK version; not included in the existing v0.1.0 release. Build matching native/Web packages from this source.

Render static GLB assets together with independent world transforms. Instances of an asset share geometry, textures and material data. Both C++ and JavaScript use the same IDs, coordinate spaces and column-major matrices.

## API

- C++: `Renderer::SetScene(assets, instances)` and `Window::SetScene(assets, instances)`.
- JavaScript: `await renderer.setScene(assets, instances)`.
- Bounds: `GetSceneAssetBounds(assetId)` / `getSceneAssetBounds(assetId)` returns the asset-local `{minimum, maximum}` in its selected space. Unknown, unreferenced or inactive assets return `std::nullopt` / `null`.

`SetScene` replaces the complete instance list and selects raster mode. It does not draw a frame. Call `DrawFrame()` / `await drawFrame()` afterwards. Empty lists clear scene geometry; the configured background is preserved. `LoadModel` / `loadModel` exits scene mode and retains its existing normalized behavior.

### Assets

Each asset has a unique nonempty string `id`, a C++ filesystem `path` or JavaScript string `url`, and an optional `sourceSpace` boolean (default `false`). Only assets referenced by instances are loaded. Asset IDs and instance IDs have separate namespaces.

- `sourceSpace: false`: center the model, scaling its longest axis to length 2. This matches the legacy single-model API.
- `sourceSpace: true`: preserve the glTF scene's authored origin, dimensions and node transforms. glTF length units are meters; Nova does not invent or correct the author's scale.

Changing only `sourceSpace` preserves the loaded geometry and textures. Bounds are axis-aligned in the selected asset-local space, before instance transforms. They include the authored glTF node transforms and are not world-space bounds.

### Instances

Each instance has a unique nonempty `id`, a registered `assetId`, and a `world` matrix. Omitting `world` uses identity in both languages. JavaScript accepts an Array or Float32Array. Matrices contain 16 finite float32-representable values, column-major and affine; translation is at indices 12–14. The matrix maps the selected asset space into world space. Normal transforms use inverse transpose for nonuniform and mirrored scales. Zero-scale instances collapse their geometry.

Nova receives final world matrices. The caller owns parent-child relationships and computes `parentWorld * local`. There is no editor hierarchy, selection or undo state in this API.

```cpp
#include <render/Window.hpp>

std::vector<render::SceneAsset> assets{{"chair", "chair.glb", true}};
std::vector<render::SceneInstance> instances{{"chair-left", "chair"}, {"chair-right", "chair"}};
instances[1].world[12] = 2.0f;
window.SetScene(assets, instances);
const auto bounds = window.GetSceneAssetBounds("chair");
window.DrawFrame();
```

```js
const assets = [{ id: 'chair', url: '/chair.glb', sourceSpace: true }]
const instances = [
  { id: 'chair-left', assetId: 'chair' },
  { id: 'chair-right', assetId: 'chair', world: [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 2, 0, 0, 1] },
]
await renderer.setScene(assets, instances)
const bounds = renderer.getSceneAssetBounds('chair')
await renderer.drawFrame()
```

## Updates and limits

Transform-only updates reuse packed geometry and textures, uploading instance data at draw time. Adding or removing referenced assets rebuilds the packed asset set. IDs plus paths/URLs identify cached assets; changing file contents at the same path is not a hot-reload operation. Validation and asset-loading failures preserve the previous scene. TAA history resets when the scene is updated, preventing stale transforms from ghosting.

This first scene API supports static opaque GLBs with the existing loader's material restrictions, all raster AA methods and HDR post-processing. Animation, picking, scene shadow maps and multi-instance ray tracing are separate features. Ray-tracing capability queries report scene instances as unsupported. Web AO currently uses the single-model BVH path: disable AO before calling `setScene`.

Materials are packed into five shared texture atlases with 2-pixel gutters. Each asset tile is capped at 2048 pixels; atlases are capped at 8192 pixels per axis and a combined 512 MiB CPU budget. Larger source textures are resampled. Atlas packing and full-mip filtering can affect texture quality; this is not yet a general multi-material system. Device memory and binding limits may impose lower practical scene sizes.

See the [scene instances example](/examples/scene).
