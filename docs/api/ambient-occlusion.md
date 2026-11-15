# 环境遮蔽 AO

AO 是独立效果设置，不新增 Mode。第八例使用白模诊断视图，材质纹理与烘焙 AO 不参与，方便观察四种算法的差异。

```cpp
#include <render/RayTracingView.hpp>
render::RayTracingView view;
view.LoadModel("assets/SponzaWhite/SponzaWhite.glb");
render::AmbientOcclusionSettings ao;
ao.enabled = true;
ao.method = render::AOMethod::HBAO;
view.SetAmbientOcclusion(ao);
window.PresentImage(view.DrawFrame(640, 480));
```

```js
await renderer.loadModel('/generated/SponzaWhite.glb')
renderer.setAmbientOcclusion({ enabled: true, method: 'hbao' })
await renderer.drawFrame()
```

两端提供 `SetAmbientOcclusion` / `GetAmbientOcclusion`（网页使用小写首字母）。参数如下：

- `enabled`：默认 false；开启后输出白模 AO 诊断图。`method` 为 Disabled / SSAO / HBAO / MSAO / RTAO，网页为 `'disabled'` / `'ssao'` / `'hbao'` / `'msao'` / `'rtao'`。Disabled 显示不加 AO 的白模，不切换渲染 Mode。
- `output`：Composite / Occlusion / Depth / Normals，网页为对应小写。默认合成，只调制环境项，保留固定方向的白模明暗；这不是给现有 PBR 材质追加完整后处理链。
- `radius`：0.001–1，默认 0.08，使用归一化模型坐标；`bias`：0–0.05，默认 0.001；`strength`：0–4，默认 1。
- `samples`：4–64，默认 16。HBAO 按方向与步数分组，MSAO 在四层深度上分别采样，不应将相同设置视为相同计算成本。
- `denoise`：默认 true，深度与法线引导的显示滤波；`resolutionScale`：0.25–1，默认 0.5；`seed`：32 位无符号整数，默认 1。

SSAO 将半球采样点投影到深度图；HBAO 搜索多个屏幕方向的遮蔽地平线；MSAO 合并原始深度与 2× / 4× / 8× 最小深度层；RTAO 使用有限半径的余弦半球可见性射线。均为 Nova 的实现，沿用旧项目的分类，并非移植完整的厂商 AO SDK。

所有平台使用共享 HLSL。当前深度/法线前置由主射线生成，为原生与网页提供一致输入；前三种算法仍只从屏幕深度估计遮蔽，无法利用被遮挡或屏幕外的表面。RTAO 原生使用现有 RayQuery 后端，网页使用 compute BVH。当前原生 AO 依赖所选后端的光追能力；这条验证路径不是已优化的光栅 G-buffer AO 实现。

`GetAccumulatedSamples()` / `getAccumulatedSamples()` 在 AO 视图中返回累计帧数，RTAO 每帧每像素发出 `samples` 条遮蔽射线。例子在 32 帧后停止；SSAO/HBAO/MSAO 按需绘制一帧。相机、尺寸和 AO 参数变化会重置。滤波只作用于显示，不反馈原始累计。

室内相机可设置 `OrbitCamera.target` / `camera.target` 为三个有限数，默认 `[0,0,0]`；距离限制仍为 1.5–100，旧例子不变。第八例的预设、面板布局和停止策略都由例子层管理。
