# Nova Render C++ SDK

This archive contains a platform-specific C++17 static library, public headers,
embedded shaders, CMake package files, bundled dependency libraries and notices.
See `share/Nova/sdk.json` for the version, architecture, compiler and build configuration.
Use the same configuration, compatible compiler ABI and Windows C++ runtime.
A Debug archive does not contain a Release build of Nova.

```cmake
find_package(Nova 0.1 CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE Nova::Render)
```

Configure your application with `-DCMAKE_PREFIX_PATH=/path/to/extracted-sdk`.
SDK consumers need a C++ compiler and CMake, but no Nova checkout, vcpkg,
DXC, SPIRV-Cross or Node.js. Do not use the vcpkg toolchain for this consumer.
GPU drivers and operating-system graphics/window services remain required.
Dynamic dependency DLLs, if present, must be distributed beside the application.
Sample models and environment maps are not bundled.

This is a local development SDK. No public distribution license has been assigned.
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
