# Scene instances

Available on `main` for the next SDK version; not included in the existing v0.1.0 release. Build matching native/Web packages from this source.

Render static GLB assets together with independent world transforms. Instances of an asset share geometry, textures and material data. Both C++ and JavaScript use the same IDs, coordinate spaces and column-major matrices.

## API

- C++: `Renderer::SetScene(assets, instances)` and `Window::SetScene(assets, instances)`.
- JavaScript: `await renderer.setScene(assets, instances)`.
- Bounds: `GetSceneAssetBounds(assetId)` / `getSceneAssetBounds(assetId)` returns the asset-local `{minimum, maximum}` in its selected space. Unknown, unreferenced or inactive assets return `std::nullopt` / `null`.

`SetScene` replaces the complete instance list and preserves the selected render mode. It does not draw a frame. Call `DrawFrame()` / `await drawFrame()` afterwards. Empty lists clear scene geometry; the configured background is preserved. `LoadModel` / `loadModel` exits scene mode and retains its existing normalized behavior.

### Assets

Each asset has a unique nonempty string `id`, a C++ filesystem `path` or JavaScript string `url`, and an optional `sourceSpace` boolean (default `false`). Only assets referenced by instances are loaded. Asset IDs and instance IDs have separate namespaces.

Native and Web scene assets use a binned surface-area heuristic (SAH) for picking and Hybrid rays by default for static assets with at least 32,768 triangles. Smaller and animated assets default to median splits. Web exposes `bvh: 'sah'` and `bvh: 'median'` to override the choice. Changing the Web strategy rebuilds the hierarchy and retains the asset's material edits.

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

## Visible outlines

`Window::SetSceneOutlines` and `Renderer::SetSceneOutlines` take a `render::SceneOutlines`. Web uses `renderer.setSceneOutlines(options)`.

```js
renderer.setSceneOutlines({
  hoveredId: 'car-2',
  selectedIds: ['car-1'],
  hovered: { color: [0.35, 0.65, 1], width: 2 },
  selected: { color: [1, 0.42, 0.08], width: 2 },
})
```

```cpp
render::SceneOutlines outlines;
outlines.hoveredId = "car-2";
outlines.selectedIds = {"car-1"};
window.SetSceneOutlines(outlines);
```

An empty options object clears both states. IDs refer to committed scene instances, with selection taking precedence over hover. Colors are linear RGB in [0, 1] and widths are 1–4 render pixels. The outline follows the projected geometry, including holes, and shows through scene occluders by default. The scene itself uses normal depth visibility. Adjacent selected pixels share a single outline group. Separate masks for hover and selection keep one group from occluding the other. The overlay supports opaque and alpha-masked geometry in the Raster and Hybrid final views. It is composited after temporal reconstruction and tone mapping. Alpha-blended objects and ray-traced views have no outline.

## Updates and limits

Transform-only updates reuse packed geometry and textures, uploading instance data at draw time. Adding or removing referenced assets rebuilds the packed asset set. IDs plus paths/URLs identify cached assets; changing file contents at the same path is not a hot-reload operation. Validation and asset-loading failures preserve the previous scene. TAA history resets when the scene is updated, preventing stale transforms from ghosting.

This first scene API supports static opaque GLBs with the existing loader's material restrictions, all raster AA methods and HDR post-processing. Static multi-instance ray tracing is supported, including world transforms, original model space, material slots and direct shadow cast flags. Animation and scene shadow maps remain separate features. [Scene picking](./picking) queries the committed instance geometry without a GPU readback. Ray-tracing capability queries now include static scene instances. Web AO currently uses the single-model BVH path: disable AO before calling `setScene`.

Materials are packed into five shared texture atlases with 2-pixel gutters. Each asset tile is capped at 2048 pixels; atlases are capped at 8192 pixels per axis and a combined 512 MiB CPU budget. Larger source textures are resampled. Atlas packing and full-mip filtering can affect texture quality; this is not yet a general multi-material system. Device memory and binding limits may impose lower practical scene sizes.

See the [scene instances example](/examples/scene).

See [Materials](./materials) for multi-slot materials and runtime factor/texture editing.

## Ray-tracing scene resources

Ray mode lazily builds a world-space triangle snapshot from shared assets and instances. Unchanged geometry reuses that snapshot and acceleration resources; light-only edits update parameters. Transform, cast-flag, asset and material updates invalidate the snapshot and reset accumulation. This first implementation rebuilds flattened geometry/acceleration data rather than using incremental TLAS updates. The expanded vertex budget is 128 MiB, in addition to device binding/memory limits; zero-scale instances contribute no geometry. Empty scenes render the configured background.

`castShadow: false` excludes an instance from direct visibility tests but keeps it visible to camera and indirect transport rays. It is not a visibility or collision mask.

## Fixed-topology geometry updates

`UpdateSceneGeometry(assetId, pose)` in C++ and
`await renderer.updateSceneGeometry(assetId, pose)` on Web update an active
procedural asset in Raster mode. Supply flat `positions` and `normals` arrays
with three components per vertex in the original triangle stream. Web also
accepts `Float32Array`. Use the same source coordinates as the geometry
submitted at creation. The original normalization remains fixed.

The renderer updates the vertex buffer range and refreshes bounds and picking.
The asset keeps its material slots, UVs, triangle order and instances. All
instances of the asset share the new geometry. Temporal raster history resets
on a pose update. Uploads on native backends currently wait for the in-flight
frame before writing the buffer.
