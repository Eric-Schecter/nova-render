# 参数面板

可选的渲染区域内 UI：C++ 使用 Dear ImGui，网页使用独立 HTML/CSS 浮层，无 WASM、无额外前端框架。两端提供相同的分组与参数范围，面板可收起，窄屏首次显示时默认收起。

## C++

```cpp
render::Window window;
window.LoadModel("assets/DamagedHelmet/DamagedHelmet.glb");
window.SetLighting(render::LightingMode::PBR);
window.EnableOrbitControls();
window.EnableParameterPanel();
while (window.PollEvents()) window.DrawFrame();
window.WaitIdle();
```

`Renderer` 同样提供 `EnableParameterPanel(bool enabled = true)`；传 `false` 隐藏面板。默认关闭，示例显式启用。返回 `false` 表示构建时未包含 UI；关闭时不会拦截模型输入。原生由主循环持续绘制，无后台线程或独立原生窗口。

通过 `-DNOVA_ENABLE_UI=OFF` 可构建不依赖 ImGui 的渲染库。启用时由 vcpkg 安装 ImGui 与所选后端绑定；安装包包含对应库和许可文件。

## 网页

```js
renderer.enableOrbitControls()
renderer.enableParameterPanel({ onError: error => console.error(error) })
// renderer.disableParameterPanel()
// 页面结束时 renderer.dispose()
```

`enableParameterPanel()` 返回清理函数，重复启用不会叠加面板；`dispose()` 自动移除面板、事件和待执行的 UI 绘制。控件自动安排重绘。面板直接使用现有模型、相机和光照 API，程序设置参数后也会同步显示；独立使用网页模块不需要 Vue。

## 控件

- Scene：深色 / 浅色背景，所有例子都有。
- Camera：水平角 ±180°、俯仰角 ±85°、距离 1.5～100、重置到启用时的相机。
- PBR 增加曝光 0.1～3，以及主光方位 ±180°、仰角 ±90°、线性 RGB 0～1、强度 0～100、重置到启用时的光源。

内置 Window / 环绕控件会隔离 UI 输入，操作面板不会触发模型旋转或缩放；面板外仍可拖动与滚轮缩放。网页面板随预览一起隐藏。曝光和灯光只出现在光照例子中，无光照模型保留基础颜色显示。

背景可通过 `SetClearColor({r,g,b,a})` / `setClearColor([r,g,b,a])` 设置，`GetClearColor()` / `getClearColor()` 返回副本。RGBA 为 0～1 的有限数，RGB 使用显示用 sRGB 值；光源颜色仍为线性 RGB。

当前已验证 macOS Metal / Vulkan、Ubuntu Lavapipe 和 Chrome WebGPU，包含输入隔离、显隐与生命周期。DX12 UI 接入已实现，尚待 Windows 实机验证。
