# 后处理

`Renderer`、`Window`、`AntiAliasingView` 与 `ShadowView` 复用同一条光栅后处理链。默认关闭；第十一个例子的 `PostProcessingView` 默认开启，使用程序化 HDR 暗室，无额外资源和依赖。这里的 HDR 指内部浮点颜色，最终输出仍为 sRGB SDR，不要求 HDR 显示器。

```cpp
render::PostProcessingSettings post;
post.enabled = true;
post.toneMapping = render::ToneMapping::ACES;
post.bloomIntensity = 0.08f;
window.SetPostProcessing(post);
window.DrawFrame();
```

```js
renderer.setPostProcessing({ enabled: true, toneMapping: 'aces', bloomIntensity: 0.08 })
await renderer.drawFrame()
```

原生通过 `GetPostProcessing()` 读取完整设置；网页对应 `getPostProcessing()`，setter 支持部分字段。非法设置抛出异常并保留原值。当前只作用于光栅绘制，不改动 AO 诊断、光追和 `PresentImage` 的输出。

- `enabled`：后处理总开关，常规 Renderer 默认 false，HDR 例子默认 true。
- `toneMapping`：Linear / Reinhard / ACES，网页为 `linear` / `reinhard` / `aces`，默认 ACES。ACES 使用 Narkowicz 拟合曲线，不是完整 ACES 色彩管理流程。
- `exposureEV`：曝光或自动曝光补偿，−8–8 EV，默认 0；每增加 1 EV，输入乘以 2。
- `autoExposure`：默认 false。对线性场景亮度做对数降采样，以几何平均亮度与 0.18 灰作为目标，按真实帧间隔指数适应。
- `minEV` / `maxEV`：自动曝光增益上下界，范围 −10–10 EV，默认 −6 / 6；min 不超过 max。
- `adaptationRate`：0.1–10，默认 1.5，越高适应越快。
- `bloom`：默认 true；总开关关闭时不执行 Bloom。
- `bloomThreshold`：线性亮度阈值 0–20，默认 1。
- `bloomKnee`：阈值软过渡 0–1，默认 0.5。
- `bloomIntensity`：0–2，默认 0.08；0 时跳过 Bloom 链。
- `saturation`：0–2，默认 1；0 为灰度。
- `contrast`：0.1–2，默认 1；在色调映射前围绕 0.18 灰调整对比度。

管线保留 RGBA16F 线性 HDR 颜色：MSAA／SSAA resolve → HDR TAA（若选用）→ 自动曝光与五级 Bloom → 颜色调整／色调映射 → FXAA 或 SMAA（若选用）→ 一次 sRGB 显示编码。Bloom 先提取高亮、逐级降采样，再以 tent 滤波逐级上采样；自动曝光归约处理非二次幂和极小尺寸。

常规模型在开启后处理后跳过材质内的旧色调映射，避免重复处理；旧相机 `exposure` 仍作为场景颜色乘数，`exposureEV` 是后处理曝光。关闭新接口时保持原有绘制路径。

```cpp
#include <render/PostProcessingView.hpp>
render::PostProcessingView view;
view.SetSceneIntensity(4.0f);
window.PresentImage(view.DrawFrame(800, 485));
```

```js
const view = await Nova.createPostProcessingView({ canvas })
view.setSceneIntensity(4)
await view.drawFrame(800, 485)
```

独立视图还提供与 AA／阴影视图相同的相机、时间、六档 AA、历史重置与绘制接口。`SceneIntensity` 范围 0.05–20、默认 1，用于展示曝光适应；`SetTime` 驱动球体移动。网页使用 `dispose()` 释放资源。UI 在自动曝光编辑后继续绘制约 8 个适应时间常数，静止后待命；隐藏时暂停。

原生仍通过离屏 `Image` 读回再显示，性能条包含所有 pass、读回与等待，不能视为纯 GPU 耗时。当前 HDR 链复用颜色、运动矢量与历史纹理，SSR 所需的通用法线／粗糙度和深度金字塔将在后续补齐。
