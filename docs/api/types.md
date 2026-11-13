# 类型与配置

## GraphicsAPI

```cpp
enum class GraphicsAPI { Auto, Vulkan, D3D12, Metal };
```

`Auto` 固定选择 macOS 的 Metal、Windows 的 D3D12、Linux 的 Vulkan。初始化失败不自动切换后端。

## RendererConfig

```cpp
struct RendererConfig {
    GraphicsAPI api = GraphicsAPI::Auto;
    bool enableValidation = true;
    bool useWarp = false;
    std::filesystem::path shaderDirectory;
};
```

- `api`：请求的后端。
- `enableValidation`：请求 Vulkan / D3D12 校验层；Metal 使用环境变量 `MTL_DEBUG_LAYER=1`。
- `useWarp`：显式选择 D3D12 CPU 软件渲染，仅对 D3D12 有效。
- `shaderDirectory`：生成的二进制 shader 目录，建议传绝对路径，不会自动定位。

资源文件：Vulkan 使用 `triangle.vert.spv` / `triangle.frag.spv`，D3D12 使用 `triangle.vert.dxil` / `triangle.frag.dxil`，Metal 使用 `triangle.metallib`。

## FrameResult

```cpp
enum class FrameResult { Rendered, Skipped };
```

`Rendered` 表示提交并安排呈现；`Skipped` 表示本次没有提交可计数的渲染帧。跳帧后继续处理窗口事件。
