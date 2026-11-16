# Backend selection

```cpp
const char* GraphicsAPIName(GraphicsAPI api);
bool IsGraphicsAPIAvailable(GraphicsAPI api);
GraphicsAPI ResolveGraphicsAPI(const RendererConfig& config);
```

- `GraphicsAPIName`: returns a static string; do not free it. Unknown values return `Unknown`.
- `IsGraphicsAPIAvailable`: checks whether the backend was compiled in, not the GPU or driver. `Auto` returns true.
- `ResolveGraphicsAPI`: resolves Auto and validates configuration without creating a window/device. Invalid WARP combinations throw `std::invalid_argument`; unavailable backends throw `std::runtime_error`.

Check the configuration before creating a window:

```cpp
render::RendererConfig config;
config.api = render::GraphicsAPI::Auto;
auto selected = render::ResolveGraphicsAPI(config);
```
