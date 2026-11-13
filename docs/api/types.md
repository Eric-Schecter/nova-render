# 类型与配置

```cpp
#include "render/Renderer.hpp"
```

## GraphicsAPI

```cpp
enum class GraphicsAPI { Auto, Vulkan, D3D12, Metal };
```

`Auto` 固定映射为 macOS → Metal、Windows → D3D12、Linux → Vulkan。它不逐个尝试后端。显式请求不可用后端会失败，初始化失败也不会切换 API。

## RendererConfig

```cpp
struct RendererConfig {
    GraphicsAPI api = GraphicsAPI::Auto;
    bool enableValidation = true;
    bool useWarp = false;
    std::filesystem::path shaderDirectory;
};
```

### api

请求的后端。可先调用 [ResolveGraphicsAPI](./selection#resolvegraphicsapi) 检查组合，再创建窗口。

### enableValidation

默认 `true`。请求 Vulkan Khronos Validation Layer 或 D3D12 Debug Layer；环境缺少校验组件时不能把计数为零理解为已经通过 API 校验。Metal 的系统 API Validation 由 `MTL_DEBUG_LAYER=1` 控制，此字段不控制它。

示例程序在 Release（定义 `NDEBUG`）时将此字段设为 `false`；这是示例行为，结构体自身的默认值仍为 `true`。

### useWarp

默认 `false`。仅允许与解析后的 D3D12 后端搭配，显式选择微软 CPU 软件适配器。普通 DX12 模式不自动回退 WARP。

### shaderDirectory

二进制 shader 所在目录。默认空路径，不自动定位资源；相对路径按进程工作目录解释。建议显式传入绝对路径。

- Vulkan：`triangle.vert.spv`、`triangle.frag.spv`。
- D3D12：`triangle.vert.dxil`、`triangle.frag.dxil`。
- Metal：`triangle.metallib`。

资源加载可能发生在构造或首次绘制时，应同时处理两处异常。

## FrameResult

```cpp
enum class FrameResult { Rendered, Skipped };
```

`Rendered` 表示本帧已提交并安排呈现，不代表屏幕扫描输出完成；`Skipped` 表示当前未提交可计数的渲染帧，例如零尺寸暂停或暂时没有可用 drawable。调用方继续处理事件，不要把跳帧计入成功绘制帧数。
