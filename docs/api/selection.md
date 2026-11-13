# 后端查询与选择

## GraphicsAPIName

```cpp
const char* GraphicsAPIName(GraphicsAPI api);
```

返回供日志显示的静态字符串：`Auto`、`Vulkan`、`D3D12`、`Metal`。未知枚举值返回 `Unknown`。无需释放返回值；不初始化设备。

## IsGraphicsAPIAvailable

```cpp
bool IsGraphicsAPIAvailable(GraphicsAPI api);
```

返回当前平台和构建是否包含该后端；`Auto` 返回 `true`。Vulkan 取决于 `NOVA_ENABLE_VULKAN`，Metal 仅 macOS，D3D12 仅 Windows。

此查询不检查驱动、GPU、shader 或校验层。返回 `true` 不保证 Renderer 能成功初始化。

## ResolveGraphicsAPI

```cpp
GraphicsAPI ResolveGraphicsAPI(const RendererConfig& config);
```

纯选择策略：解析 `Auto`，检查后端和 WARP 组合，返回具体 API。不会创建窗口、设备或读取 shader。

- WARP 与解析后的非 D3D12 后端组合：抛出 `std::invalid_argument`。
- 后端未编译、平台不支持或枚举值无效：抛出 `std::runtime_error`。

检查顺序是先解析 Auto，再检查 WARP，最后检查可用性。

```cpp
render::RendererConfig config;
config.api = render::GraphicsAPI::Auto;
auto selected = render::ResolveGraphicsAPI(config);
// 可在初始化 GLFW 和创建窗口之前执行。
```
