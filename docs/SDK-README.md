# Nova Render C++ SDK

This archive contains a platform-specific C++17 static library, public headers,
embedded shaders, CMake package files, bundled dependency libraries and notices.
See `share/Nova/sdk.json` for the version, architecture, compiler and build configuration.
Use the same configuration, compatible compiler ABI and Windows C++ runtime.
A Debug archive does not contain a Release build of Nova.

```cmake
find_package(Nova 0.8 CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE Nova::Render)
```

Configure your application with `-DCMAKE_PREFIX_PATH=/path/to/extracted-sdk`.
SDK consumers need a C++ compiler and CMake, but no Nova checkout, vcpkg,
DXC, SPIRV-Cross or Node.js. Do not use the vcpkg toolchain for this consumer.
GPU drivers and operating-system graphics/window services remain required.
Dynamic dependency DLLs, if present, must be distributed beside the application.
Sample models and environment maps are not bundled.

Official archives are available from [GitHub Releases](https://github.com/Eric-Schecter/nova-render/releases).
No public distribution license has been assigned.
Third-party notices are under `share/Nova/licenses` and `share/nova/licenses`.

## Static scene instances

```cpp
std::vector<render::SceneAsset> assets{{"model", "model.glb", true}};
std::vector<render::SceneInstance> instances{{"first", "model"}, {"second", "model"}};
instances[1].world[12] = 2.f;
window.SetScene(assets, instances);
const auto bounds = window.GetSceneAssetBounds("model");
window.DrawFrame();
```

Instances share resources by asset ID. `sourceSpace=true` preserves authored glTF units and origin; the default normalizes the model. Matrices are column-major and default to identity. `SetScene({}, {})` clears geometry. This API supports static raster scenes with OPAQUE, MASK and weighted OIT BLEND materials; see the [scene API](https://eric-schecter.github.io/nova-render/api/scene.html) for limits.

## Camera and viewport

Use `SetViewCamera(Camera)` for complete pose/lens settings and `SetViewport(Viewport{...})` for a framebuffer-pixel rectangle. `SetViewport(std::nullopt)` restores the automatic viewport. Existing `SetCamera(OrbitCamera)` remains available.

See [camera and viewport](https://eric-schecter.github.io/nova-render/api/camera.html) for defaults, validation and examples.

The editor viewport can draw a procedural XZ grid with depth occlusion. Load a model or scene and enable the grid on `render::Window` or `render::Renderer`.

```cpp
render::ViewportGridSettings grid;
grid.enabled = true;
grid.minorSpacing = 0.25f;
grid.majorSpacing = 1.0f;
window.SetViewportGrid(grid);
```

The Web equivalent is `renderer.setViewportGrid({ enabled: true, minorSpacing: 0.25, majorSpacing: 1 })`.
The optional `elevation` sets the plane's world Y coordinate. `lineWidth` is measured in render pixels. `minorColor` and `majorColor` are linear RGBA. The grid is available in final raster and Hybrid model views, using Deferred with SMAA or other compatible AA settings. It fades subpixel lines and uses opaque/MASK scene depth. A separate ground mesh can still receive shadows. Keep editor snapping and grid visibility controls in the application.


## Scene picking

```cpp
render::Ray ray;
ray.origin = {0, 0, 5};
ray.direction = {0, 0, -1};
const auto hit = window.PickScene(ray);
```

This synchronous query returns `std::optional<SceneHit>` for the committed static scene.

Optional `includeIds` / `excludeIds` filter instance IDs; exclusions win. Hits contain `id`, world `position`, unit geometric `normal` and world-space `distance`. [Picking API](https://eric-schecter.github.io/nova-render/api/picking.html).

Materials: read `GetMaterials()` / `getMaterials()`, edit a slot, then call `SetMaterial(slot, material)` / `await setMaterial(slot, material)`. Pass the scene asset ID as the final argument to edit all its instances. Texture maps accept RGBA8 images; `LoadMaterialImage(path)` / `await loadMaterialImage(url)` decodes an image. See the materials API documentation for ranges and atlas limits.

The SDK includes formal scene, camera, picking, material, GPU shadow and atmosphere APIs. Native/Web packages carry their source commit. Use `scripts/packaging/pair.mjs` to create a SHA-256 manifest; mixing revisions or dirty builds is rejected. See the online [installation guide](https://eric-schecter.github.io/nova-render/guide/package.html) for release downloads.

SDK 0.8.0: `alphaMode`, `alphaCutoff`, `doubleSided` and RGBA alpha are imported and editable. Query material capabilities before choosing ray tracing or BLEND AA combinations.

## Geometry from memory

The core archive (`nova-render-sdk-core-…`) is built with
`-DNOVA_ENABLE_GLTF=OFF -DNOVA_BUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF`. It omits
TinyGLTF and its JSON dependency. The compatibility archive enables glTF file
loading. CMake consumers can check `Nova_GLTF_FILE_LOADING`.

Both builds accept geometry through `SetScene`:

```cpp
auto mesh = std::make_shared<render::SceneGeometry>();
mesh->vertices = {
    {{0, 0, 0}, {0, 0, 1}},
    {{1, 0, 0}, {0, 0, 1}},
    {{0, 1, 0}, {0, 0, 1}},
};
render::SceneAsset asset;
asset.id = "triangle";
asset.sourceSpace = true;
asset.geometry = mesh;
render::SceneInstance left{"left", asset.id}, right{"right", asset.id};
right.world[12] = 2;
window.SetScene({asset}, {left, right});
```

An asset accepts either a file path or an immutable geometry object. Memory
geometry uses a non-indexed triangle list with optional material definitions
and zero-based `triangleMaterials`. Supply a new geometry object to replace
the mesh. Instances with the same asset ID reuse the same geometry.
