# Nova Render C++ SDK

This archive contains a platform-specific C++17 static library, public headers,
embedded shaders, CMake package files, bundled dependency libraries and notices.
See `share/Nova/sdk.json` for the version, architecture, compiler and build configuration.
Use the same configuration, compatible compiler ABI and Windows C++ runtime.
A Debug archive does not contain a Release build of Nova.

```cmake
find_package(Nova 0.2 CONFIG REQUIRED)
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

Instances share resources by asset ID. `sourceSpace=true` preserves authored glTF units and origin; the default normalizes the model. Matrices are column-major and default to identity. `SetScene({}, {})` clears geometry. This API supports static opaque raster scenes; see the [scene API](https://eric-schecter.github.io/nova-render/api/scene.html) for limits.

## Camera and viewport

Use `SetViewCamera(Camera)` for complete pose/lens settings and `SetViewport(Viewport{...})` for a framebuffer-pixel rectangle. `SetViewport(std::nullopt)` restores the automatic viewport. Existing `SetCamera(OrbitCamera)` remains available.

See [camera and viewport](https://eric-schecter.github.io/nova-render/api/camera.html) for defaults, validation and examples.

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

The 0.2 SDK includes formal scene, camera, picking, material, GPU shadow and atmosphere APIs. Native/Web packages carry their source commit. Use `scripts/packaging/pair.mjs` to create a SHA-256 manifest; mixing revisions or dirty builds is rejected. See the online [installation guide](https://eric-schecter.github.io/nova-render/guide/package.html) for release downloads.
