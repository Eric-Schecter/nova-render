# Renderer

```cpp
#include "render/Renderer.hpp"
```

`Renderer` 管理 GPU 资源，应用管理 GLFW 窗口和事件。所有调用在窗口主线程执行；当前只支持同时存在一个 Renderer，不支持复制或移动。

## 创建与销毁

```cpp
render::RendererConfig config;
config.shaderDirectory = APP_SHADER_DIR;
render::Renderer renderer(window, config);
```

GLFW 须先初始化，窗口用 `GLFW_NO_API` 创建。销毁顺序是 Renderer → 窗口 → GLFW。配置或设备不可用、资源加载失败时会抛异常；部分资源在首次绘制时才创建。

## 常用方法

```cpp
void Resize(uint32_t framebufferWidth, uint32_t framebufferHeight);
FrameResult DrawFrame();
void WaitIdle();
GraphicsAPI GetAPI() const;
uint32_t ValidationErrorCount() const;
```

- `Resize`：首次绘制前和尺寸变化后传入 framebuffer 像素尺寸。任一维为零时暂停，资源重建在绘制时执行。
- `DrawFrame`：绘制内置三角形或已加载模型；返回 `Rendered` 或 `Skipped`。不处理窗口事件，失败可抛异常。
- `WaitIdle`：等待已提交工作完成，退出前调用；执行错误可能在此报告。
- `GetAPI`：返回实际选择的具体后端。
- `ValidationErrorCount`：读取后端累计错误数。建议等待 GPU 后读取；零计数不代表画面一定正确或校验层已开启。

完整用法见[编写示例](../guide/integration)。

模型模式通过 `LoadModel(path)` 和 `SetCamera(camera)` 设置，详见[模型与相机](./model)。
