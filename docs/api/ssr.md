# 屏幕空间反射

`ScreenSpaceReflectionView` 封装第十二个程序化场景，两端共享 HLSL，支持 Metal、Vulkan、DX12 和 WebGPU，无新增依赖。

```cpp
#include <render/ScreenSpaceReflectionView.hpp>
render::ScreenSpaceReflectionView view;
auto settings = view.GetScreenSpaceReflections();
settings.hierarchical = true;
view.SetScreenSpaceReflections(settings);
window.PresentImage(view.DrawFrame(800, 485));
```

```js
const view = await Nova.createScreenSpaceReflectionView({ canvas })
view.setScreenSpaceReflections({ hierarchical: true, maxSteps: 128 })
await view.drawFrame(800, 485)
// 页面退出：view.dispose()
```

光栅通道输出线性 HDR 颜色、运动矢量、世界空间法线／粗糙度和线性视空间深度。随后建立完整的 min/max 深度金字塔（非二次幂尺寸也覆盖到 1×1），使用屏幕空间 DDA 分层跳过不相交区域，在最细层做深度比较和二分细化。

命中颜色以粗糙度相关的九点滤波读取，并抑制跨深度混色；不是多条 GGX 随机射线。命中失败、超出屏幕、过粗糙或达到追踪预算时，回退到预过滤环境反射；屏幕边缘、远距离和粗糙度边界平滑混合。关闭 SSR 仍保留环境反射。

- `enabled`：默认 true。
- `hierarchical`：默认 true；false 使用线性屏幕步进，便于对照。
- `maxSteps`：16–256，默认 128，包含层级下降和推进的迭代。
- `maxDistance`：1–60 个场景单位，默认 24。
- `thickness`：0.01–1，默认 0.12，视空间深度容差；过大会穿透薄物体。
- `stride`：0.5–4 个屏幕像素，默认 1，仅线性追踪使用。
- `roughnessCutoff`：0.05–1，默认 0.8，超过时使用环境反射。
- `edgeFade`：0.005–0.3，默认 0.08，命中位置离屏幕边缘的归一化混合宽度。
- `intensity`：0–2，默认 1，控制合成反射强度。
- `environmentIntensity`：0–4，默认 1，控制环境照明／回退亮度。
- `debug`：Composite、Reflections、HitMask、Normals、Roughness、LinearDepth、DepthPyramid。网页为 `composite`、`reflections`、`hit-mask`、`normals`、`roughness`、`linear-depth`、`depth-pyramid`。
- `debugMip`：0–12，默认 3；0 为原始深度，超出当前图像可用层数时钳制到最粗层。

`SetScreenSpaceReflections()` / `GetScreenSpaceReflections()` 读写设置；网页对应小写首字母，setter 支持部分字段。非法设置抛出异常并保留原值。

`LoadEnvironment(path)` / `loadEnvironment(url)` 加载项目已有的 `.nenv` 环境资源（网页使用对应 `.bin`）；`UseDefaultEnvironment()` / `useDefaultEnvironment()` 恢复内置程序化 HDR 摄影棚。预过滤环境保留 HDR 值，网页示例提供四种环境选择。

沿用相机、时间、六档 AA、后处理和历史重置接口。SSR 在 HDR TAA 和后处理之前合成；运动矢量来自真实相机与物体变换，TAA 在命中置信度明显变化时拒绝旧反射，减少遮挡边缘残影。调试数据视图绕过色调映射／Bloom，以便观察输入。

MSAA 使用真实四重采样和硬件 resolve，SSAA 在内部双倍宽高上追踪反射再降采样。输出范围为 1–2048；示例长边上限 1024、默认渲染比例 75%，不自动降低画质。性能条包括表面通道、金字塔、追踪、AA、后处理及等待；原生仍包含 `Image` 读回，不代表纯 GPU 计时。

当前接口用于内置场景，尚未接入任意 GLB 的 Renderer。SSR 只能使用当前屏幕可见的第一层表面，不能重建屏幕外或被遮挡的物体；环境回退并非这些物体的真实镜像。深度、法线与粗糙度调试图可用于核对这一限制。
