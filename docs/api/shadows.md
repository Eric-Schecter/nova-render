# 阴影

`ShadowView` 封装程序化阴影场景，使用真实光源视角阴影贴图，支持 Metal、Vulkan、DX12 和 WebGPU。无需模型资源或新增依赖；两端共享 HLSL。

```cpp
#include <render/ShadowView.hpp>
render::ShadowView view;
auto shadows = view.GetShadowSettings();
shadows.method = render::ShadowMethod::PCSS;
view.SetShadowSettings(shadows);
window.PresentImage(view.DrawFrame(800, 485));
```

```js
const view = await Nova.createShadowView({ canvas })
view.setShadowSettings({ method: 'pcss', lightSize: 0.35 })
await view.drawFrame(800, 485)
// 页面退出时：view.dispose()
```

- `method`：Off / Hard / PCF / PCSS，网页为 `off` / `hard` / `pcf` / `pcss`，默认 PCSS。
- `resolution`：512、1024、2048，默认 1024。
- `depthBias`：0–0.02，默认 0.001；增加可缓解自阴影痤疮，过大会使投影脱离物体。
- `normalBias`：0–0.1，默认 0.015；沿法线偏移采样点，斜面偏移更大。
- `filterRadius`：1–6 个阴影纹素，默认 2，仅 PCF 使用。
- `lightSize`：0.01–1.5 个场景单位，默认 0.35，仅 PCSS 使用。
- `lightDirection`：从表面指向光源的非零三维向量，默认 `[-1.5, 3, 2]`。

提供 `SetShadowSettings` / `GetShadowSettings`、`SetAntiAliasing` / `GetAntiAliasing`、`SetCamera` / `GetCamera`、`SetTime`、`ResetHistory`、`GetHistoryFrames`、`DrawFrame`；网页对应小写首字母。网页 setter 支持部分字段；原生传完整设置。设置非法时抛出异常，保留原设置。

AA 默认 TAA，支持 Off、FXAA、SMAA 1x、MSAA 4x、SSAA 2×2、TAA。设置或阴影参数改变会重置历史，小幅相机与物体运动使用运动矢量重投影。相机角度单位为度、距离范围 4–20、俯仰 ±60°，输出尺寸为 1–2048；展示页长边上限 1024，不自动降低画质。

Hard Shadow 单次深度比较；PCF 使用 32 个圆盘样本过滤比较结果；PCSS 使用 24 个样本查找遮挡物，再依据遮挡物与接收面的距离，以 32 个样本估计半影。使用固定覆盖范围的单张阴影贴图，PCSS 是软阴影近似，不是光追；不包含级联阴影、点光源或透射阴影。

当前接口用于内置测试场景，尚未将阴影设置接入任意 GLB 模型的 `Renderer`。原生离屏结果读回 `Image` 再显示，网页直接显示，性能条包含全部阴影、AA、读回与等待成本。
