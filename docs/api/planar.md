# 平面反射

`PlanarReflectionView` 封装第十三个内置场景，C++ 与网页共用 HLSL，无新增依赖。

```cpp
#include <render/PlanarReflectionView.hpp>
render::PlanarReflectionView view;
auto reflections = view.GetPlanarReflections();
reflections.floorRoughness = 0.1f;
reflections.mirrorIntensity = 0.8f;
view.SetPlanarReflections(reflections);
window.PresentImage(view.DrawFrame(800, 485));
```

```js
const view = await Nova.createPlanarReflectionView({ canvas })
view.setPlanarReflections({ floorRoughness: 0.1, mirrorIntensity: 0.8 })
await view.drawFrame(800, 485)
// 页面退出：view.dispose()
```

`SetPlanarReflections()` / `GetPlanarReflections()` 对应网页的 `setPlanarReflections()` / `getPlanarReflections()`，网页 setter 支持部分字段。

- `floorEnabled` / `mirrorEnabled`：启用地板／竖直镜子的平面反射，默认 true；关闭时使用环境反射。
- `floorRoughness` / `mirrorRoughness`：0–1，默认 0.06／0.02。
- `floorIntensity` / `mirrorIntensity`：0–2，默认 1；设为 0 消除对应表面的反射贡献。

非法数值抛出异常并保留原设置。沿用相机、时间、环境加载、六档 AA、后处理与历史重置接口，命名与 [SSR](./ssr) 相同。

每个启用的平面额外绘制一次完整场景，以反射变换投影几何，等价于镜像相机；保持原始世界坐标用于光照，并裁剪平面背后的几何。反射图包含 HDR 天空背景，不依赖主画面颜色，因此能显示主视野外的物体。随后只在对应接收表面合成，并在 AA 和后处理前完成。TAA 使用镜像视图中物体的运动矢量，而非仅使用镜面本身的运动；高粗糙度、多层运动仍可能存在近似残影。

当前是单层反射：反射视图省略地板和镜子，避免相互递归。镜面为地板 y=-1、竖直镜子 z=-3.2，仅用于内置程序化场景，尚未开放任意场景与自定义反射平面。相机应位于地板上方、镜子正面。粗糙度采用反射图上的 25 点高斯近似模糊，不是物理微表面多射线积分；高粗糙度或遮挡边缘仍可能出现近似误差。

反射图与场景使用相同内部尺寸；SSAA 在双倍宽高绘制，MSAA 4x 用于主场景，反射图保持单采样。性能条包含镜像场景绘制、合成、AA、后处理及等待，原生还包括图像读回，不等于纯 GPU 耗时。
