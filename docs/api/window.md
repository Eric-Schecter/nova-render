# Window

推荐示例入口，头文件 `render/Window.hpp`。通过 PImpl 隐藏窗口依赖，内部拥有窗口、事件系统和 Renderer。

## 配置

```cpp
struct WindowConfig {
    uint32_t width = 800;
    uint32_t height = 600;
    std::string title = "Nova";
    RendererConfig renderer;
};
```

宽高为窗口逻辑尺寸，须为正数且不超过 int 上限；实际 framebuffer 像素尺寸由内部查询。渲染参数见 [RendererConfig](./types)。

## 接口

```cpp
explicit Window(const WindowConfig& config = {});
~Window();
bool PollEvents();
FrameResult DrawFrame();
void WaitIdle();
void LoadModel(const std::filesystem::path& glbPath);
void SetCamera(const OrbitCamera& camera);
OrbitCamera GetCamera() const;
void EnableOrbitControls(bool enabled = true);
GraphicsAPI GetAPI() const;
uint32_t ValidationErrorCount() const;
```

- `PollEvents`：处理事件，关闭窗口或按 Esc 后返回 false。
- `DrawFrame`：同步像素尺寸并绘制；零尺寸暂停，跳帧时短暂等待事件。
- 其余方法转发给内部 Renderer，含义与 [Renderer](./renderer) 相同。

仅在主线程使用，不可复制或移动。当前只允许一个 Window，不与外部管理的 GLFW 生命周期混用。构造会检查配置；窗口或渲染初始化失败抛异常并清理资源。退出前调用 `WaitIdle()` 获取可能延迟报告的 GPU 错误；析构按 Renderer → 窗口 → 事件系统顺序释放。

完整代码见[编写示例](../guide/integration)。底层 Renderer 仍适合需要自行管理窗口的集成。

模型加载和相机配置见[模型与相机](./model)。
