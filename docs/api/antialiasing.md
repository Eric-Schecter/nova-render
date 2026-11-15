# 抗锯齿

`Renderer` 和 `Window` 的常规光栅绘制支持六档 AA，覆盖三角形、模型、光照、天空盒和 GPU 蒙皮。默认 Off，参数面板提供算法选择；不新增 Mode。AO 诊断与光追采用各自的采样流程，不叠加这里的光栅 AA。

```cpp
render::AntiAliasingSettings aa;
aa.method = render::AAMethod::TAA;
window.SetAntiAliasing(aa);
window.DrawFrame();
```

```js
renderer.setAntiAliasing({ method: 'taa' })
await renderer.drawFrame()
```

通过 `GetAntiAliasingHistoryFrames()` / `getAntiAliasingHistoryFrames()` 查看 TAA 历史帧数。小幅相机移动和 GPU 蒙皮使用上一帧的矩阵与骨骼姿态重投影；切换模型、环境、曝光、算法、尺寸或大幅切换视角时重置历史。网页静止后累计 32 帧即待命，动画播放时继续绘制。

模型纹理默认生成完整 mip 链，使用三线性与最高 8× 各向异性过滤（Vulkan 按设备支持降级）；基础色与自发光在 sRGB/线性空间正确转换，法线、材质与遮蔽数据保持线性。无需增加依赖。

`AntiAliasingView` 是独立的程序化测试台，额外提供分屏、放大、时间和棋盘过滤开关：

```cpp
#include <render/AntiAliasingView.hpp>
render::AntiAliasingView view;
render::AntiAliasingSettings settings;
settings.method = render::AAMethod::TAA;
view.SetAntiAliasing(settings);
view.SetTime(0.1f);
window.PresentImage(view.DrawFrame(800, 485));
```

```js
const view = await Nova.createAntiAliasingView({ canvas, shaderDirectory: '/generated/' })
view.setAntiAliasing({ method: 'taa', split: true })
view.setTime(0.1)
await view.drawFrame(800, 485)
```

两端提供 `SetAntiAliasing` / `GetAntiAliasing`、`SetCamera` / `GetCamera`、`SetTime`、`ResetHistory`、`GetHistoryFrames`、`DrawFrame`。网页使用小写首字母，并提供 `dispose()`；原生对象离开作用域时释放资源。网页 setter 可传部分字段，原生传完整设置。

- `textureFiltering`：默认 true，测试台的程序化棋盘使用屏幕导数解析过滤；关闭可观察高频纹理混叠。此开关不关闭模型的通用纹理过滤。
- `method`：Off / FXAA / SMAA / MSAA / SSAA / TAA，网页为对应小写，默认 TAA。
- `historyWeight`：0–0.98，默认 0.9，仅 TAA 使用。越高越依赖历史，运动时可能损失细节。
- `zoom`：1–8，默认 1。全画面以中心为基准放大，便于观察像素。
- `split`：默认 true。左侧显示同一时刻无抖动、无 AA 的基准图，右侧显示所选算法。

`OrbitCamera` 的 yaw、pitch、distance、exposure 用于此固定中心场景，默认 20° / 15° / 6.2 / 1；距离限制 4–20，俯仰 ±60°，曝光 (0,4]。`SetTime` 接收 0–1,000,000 秒。输出尺寸为整数 1–2048；例子按比例渲染，输出长边上限 1024，SSAA 的内部宽高为输出的两倍。

实现使用同一份 HLSL，原生为 Metal / Vulkan / DX12，网页为 WebGPU：

- **Off**：单采样光栅基准。
- **FXAA**：NVIDIA FXAA 3.11，quality 29；用 gamma 亮度 alpha 检测边缘，对线性颜色混合。
- **SMAA 1x**：作者参考实现 HIGH 预设，边缘检测 → Area/Search 查表计算混合权重 → 邻域混合。只提供空间 1x，不含 SMAA T2x / S2x / 4x。
- **MSAA 4x**：真正的四重采样颜色与深度目标及硬件 resolve。主要处理几何覆盖边缘，无法独自消除像素着色器里的高频棋盘闪烁。
- **SSAA 2×2**：宽高各两倍，每输出像素四个完整着色样本，在**线性颜色**中降采样，像素着色与存储成本更高。
- **TAA**：8 相位 Halton 抖动，当前/上一帧变换产生运动矢量，历史重投影、深度与材质拒绝、3×3 邻域夹取。算法、权重切换、尺寸变化、时间跳变和大幅相机切换会清除历史；小幅运动沿矢量重投影。当前没有运动模糊、锐化或超分辨率。

`GetHistoryFrames()` 只对 TAA 返回自重置后的历史帧数，其他方法返回 0。例子暂停后让 TAA 再绘制 32 帧，其他方法按需绘制；隐藏或离开视口时网页暂停。UI、运动节奏与停止预算属于例子层。

原生启用 AA 后，离屏结果读回为 `Image` 再交给窗口显示；网页直接显示。Draw / FPS 包含绘制、读回与等待，不代表纯 GPU 成本。SSAA 的内部宽高各加倍，MSAA 使用真实四重采样；资源按所选算法分配，不自动降画质。
