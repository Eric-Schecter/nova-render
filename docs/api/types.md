# Types and configuration

## GraphicsAPI

```cpp
enum class GraphicsAPI { Auto, Vulkan, D3D12, Metal };
```

`Auto` selects Metal on macOS, D3D12 on Windows, and Vulkan on Linux. Initialization failure never silently switches backends.

## RendererConfig

```cpp
struct RendererConfig {
    GraphicsAPI api = GraphicsAPI::Auto;
    bool enableValidation = true;
    bool useWarp = false;
    std::filesystem::path shaderDirectory;
};
```

- `api`: requested backend.
- `enableValidation`: requests Vulkan / D3D12 validation; use `MTL_DEBUG_LAYER=1` for Metal.
- `useWarp`: explicitly requests CPU D3D12 WARP; invalid with other APIs.
- `shaderDirectory`: optional external binary directory. Empty uses embedded shaders; a nonempty path loads strictly from that directory. Prefer an absolute path.

Vulkan uses `triangle.vert.spv` / `triangle.frag.spv`, D3D12 uses `.dxil`, and Metal uses `triangle.metallib`. Model and skybox shaders follow the same backend naming and are embedded by default. External directories must contain all shaders used by the application.

## FrameResult

```cpp
enum class FrameResult { Rendered, Skipped };
```

`Rendered` means work was submitted and presentation scheduled; `Skipped` means no countable render frame was submitted. Continue processing events after a skipped frame.

## DirectionalLight

`direction` is a world-space vector from the surface toward the light, `color` is linear RGB, and `intensity` is a multiplier. Native and web defaults and ranges match; see [lighting](./model#lighting).

## EnvironmentSettings

Rotation, lighting intensity, background brightness and visibility use matching native/web concepts. See [environment settings](./environment#settings).
