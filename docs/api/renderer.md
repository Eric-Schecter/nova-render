# Renderer

```cpp
class Renderer;
```

拥有 GPU 后端和渲染资源，通过 PImpl 隐藏平台 API。必须在窗口主线程使用；当前仅支持同时存在一个 Renderer。不可复制；当前接口也没有提供移动操作。

## 构造与析构

```cpp
Renderer(GLFWwindow* window, const RendererConfig& config);
~Renderer();
```

GLFW 须已初始化，窗口使用 `GLFW_NO_API` 创建，且必须比 Renderer 活得更久。Renderer 借用窗口，不负责销毁窗口或终止 GLFW。

构造时执行后端选择和初始化。空窗口抛出 `std::invalid_argument`；非法配置、设备/资源创建失败可抛异常，参见[选择策略](./selection)。后端运行错误通常以 `std::runtime_error` 报告，也可能传播分配或文件系统等标准异常。Vulkan 部分资源延迟到绘制时创建。

初始化失败会清理已创建资源。析构负责等待和资源释放；需要把等待错误纳入应用结果时，应在对象仍存活时显式调用 `WaitIdle()`。

销毁顺序：Renderer → GLFWwindow → `glfwTerminate()`。

## Resize

```cpp
void Resize(uint32_t framebufferWidth, uint32_t framebufferHeight);
```

传入 **framebuffer 像素尺寸**，不是窗口逻辑尺寸。在首次绘制前及尺寸变化后调用。任一维为零表示暂停绘制。这里只记录尺寸，必要的资源重建发生在 `DrawFrame()`；本函数不处理窗口事件或等待恢复。

## DrawFrame

```cpp
FrameResult DrawFrame();
```

按当前尺寸绘制固定彩色三角形，返回 [FrameResult](./types#frameresult)。零尺寸返回 `Skipped`；其他暂不可绘制状态也可能跳过。设备、shader、交换链或命令执行失败可抛异常，调用方需退出当前渲染流程并报告错误。当前设计不提供自动设备恢复。

## WaitIdle

```cpp
void WaitIdle();
```

等待已提交的 GPU 工作完成，可能阻塞，并可能报告执行错误。适合退出前或需要确认 GPU 工作结束的边界使用，无需在应用每帧额外调用。各后端当前优先采用简单的单 CPU 在途帧策略。

## GetAPI

```cpp
GraphicsAPI GetAPI() const;
```

返回已选择的具体 API，而非请求中的 `Auto`。

## ValidationErrorCount

```cpp
uint32_t ValidationErrorCount() const;
```

返回后端当前累计的错误数。Vulkan 来自 debug messenger，D3D12 来自 InfoQueue 的 error/corruption 消息，Metal 来自等待时检查到的 command buffer 错误。三种计数口径并不完全相同，Metal 系统 API Validation 日志不等同于该计数。

建议先 `WaitIdle()`，再读取计数；零计数不能证明图像正确或校验层已启用。析构期间的后续日志不包含在此前读出的计数中。
