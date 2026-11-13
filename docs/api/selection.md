# 后端查询与选择

```cpp
const char* GraphicsAPIName(GraphicsAPI api);
bool IsGraphicsAPIAvailable(GraphicsAPI api);
GraphicsAPI ResolveGraphicsAPI(const RendererConfig& config);
```

- `GraphicsAPIName`：返回静态名称字符串，无需释放；未知值返回 `Unknown`。
- `IsGraphicsAPIAvailable`：检查当前构建是否包含后端，不检测 GPU 或驱动；`Auto` 返回 `true`。
- `ResolveGraphicsAPI`：解析 Auto 并检查配置，不创建窗口或设备。不合法的 WARP 组合抛出 `std::invalid_argument`，不可用后端抛出 `std::runtime_error`。

可在创建窗口前检查配置：

```cpp
render::RendererConfig config;
config.api = render::GraphicsAPI::Auto;
auto selected = render::ResolveGraphicsAPI(config);
```
