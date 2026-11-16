# Window

推荐示例入口，头文件 `render/Window.hpp`。通过 PImpl 隐藏窗口依赖，内部拥有窗口、事件系统和 Renderer。

`SetLighting(LightingMode::Unlit / PBR)` 选择基础颜色或内置 PBR 光照，默认 Unlit。详见[模型与相机](./model)。

`SetDirectionalLight(light)` / `GetDirectionalLight()` 设置和读取主方向光（方向、线性 RGB、强度）。

## 配置

```cpp
struct WindowConfig {
    uint32_t width = 800;
    uint32_t height = 600;
    std::string title = "Nova Render";
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
void SetAntiAliasing(const AntiAliasingSettings& settings);
AntiAliasingSettings GetAntiAliasing() const;
uint32_t GetAntiAliasingHistoryFrames() const;
void LoadModel(const std::filesystem::path& glbPath);
void LoadEnvironment(const std::filesystem::path& environmentPath);
void SetEnvironment(const EnvironmentSettings& settings);
EnvironmentSettings GetEnvironment() const;
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

`EnableParameterPanel(bool enabled = true)` 控制渲染区域内的可选参数面板，见[参数面板](./ui)。

天空盒和 HDR 环境光见[天空盒与环境光](./environment)。

`EnableParameterPanel(const ParameterPanelConfig&)` 可配置模型与环境选择列表，见[参数面板](./ui#模型与环境选择)。

光栅与光追的模式、采样和能力查询接口见[渲染模式](./render-mode)。

## 应用组织的视图

`GetFramebufferSize()` 返回当前帧缓冲的像素宽高；窗口最小化时可返回零。`PresentImage(image)` 复制 sRGB RGBA8 图像并在下一帧显示；`ClearImage()` 恢复 Window 自己的光栅或光追输出。由应用决定图像布局、预算与 UI，例如[积分器对比](../examples/integrators)。

光栅 AA 默认 Off，六档算法与历史管理见[抗锯齿](./antialiasing)。
