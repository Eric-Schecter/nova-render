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
- PBR 或已加载环境时提供曝光 0.1～3；PBR 提供主光方位 ±180°、仰角 ±90°、线性 RGB 0～1、强度 0～100、重置到启用时的光源。

内置 Window / 环绕控件会隔离 UI 输入，操作面板不会触发模型旋转或缩放；面板外仍可拖动与滚轮缩放。网页面板随预览一起隐藏。无光照模型保留基础颜色显示；加载环境后，Environment 分组提供天空盒显隐、旋转 ±180°、环境强度与天空亮度 0～16、重置。天空盒显示时，背景预设暂时禁用。

背景可通过 `SetClearColor({r,g,b,a})` / `setClearColor([r,g,b,a])` 设置，`GetClearColor()` / `getClearColor()` 返回副本。RGBA 为 0～1 的有限数，RGB 使用显示用 sRGB 值；光源颜色仍为线性 RGB。

当前已验证 macOS Metal / Vulkan、Ubuntu Lavapipe 和 Chrome WebGPU，包含输入隔离、显隐与生命周期。本轮也通过 Windows 虚拟机 DX12 WARP 的桌面会话检查；本轮未验证 Windows 真机 GPU。

## 模型与环境选择

三个模型例子均可选择 Damaged Helmet、Avocado、BoomBox；天空盒例子还提供三套 HDR 环境。切换保留相机、曝光、灯光和环境参数。

```cpp
render::ParameterPanelConfig panel;
panel.models = {
    {"Avocado", "assets/Avocado/Avocado.glb"},
    {"BoomBox", "assets/BoomBox/BoomBox.glb"}
};
panel.environments = {
    {"Venice Sunset", "assets/Environment/venice_sunset.nenv"},
    {"Studio Small 09", "assets/Environment/studio_small_09.nenv"}
};
window.EnableParameterPanel(panel); // Renderer 同样支持
```

`AssetChoice` 包含显示名 `name` 和资源路径 `path`，由应用提供。启用面板不会加载列表第一项，初始资源仍用 `LoadModel` / `LoadEnvironment` 设置。UI 请求在下一帧开始前执行；加载失败保留原资源并在面板显示错误。省略列表时隐藏对应选择器。

```js
renderer.enableParameterPanel({
  models: [
    { name: 'Avocado', url: '/generated/Avocado.glb' },
    { name: 'BoomBox', url: '/generated/BoomBox.glb' },
  ],
  environments: [
    { name: 'Venice Sunset', url: '/generated/venice_sunset.nenv' },
    { name: 'Studio Small 09', url: '/generated/studio_small_09.nenv' },
  ],
  onError: error => console.error(error),
})
```

网页按需异步加载，期间禁用选择器并保留原画面；失败时恢复原选择，通过 `onError` 报告。`dispose()` 清理资源与事件。
