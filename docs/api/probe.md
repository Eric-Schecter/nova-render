# 反射探针

`ReflectionProbeView` 封装第十四个内置场景，C++ 和网页共用 HLSL，无新增依赖。

```cpp
#include <render/ReflectionProbeView.hpp>
render::ReflectionProbeView view;
auto probe = view.GetReflectionProbe();
probe.boxProjection = true;
probe.position = {1, 0.6f, 0};
view.SetReflectionProbe(probe);
window.PresentImage(view.DrawFrame(800, 485));
```

```js
const view = await Nova.createReflectionProbeView({ canvas })
view.setReflectionProbe({ boxProjection: true, position: [1, 0.6, 0] })
await view.drawFrame(800, 485)
// 页面退出：view.dispose()
```

`SetReflectionProbe()` / `GetReflectionProbe()` 对应网页的 `setReflectionProbe()` / `getReflectionProbe()`；网页 setter 支持部分字段，数组按值复制。

- `enabled`：启用局部 Cubemap，默认 true；关闭时使用全局环境反射。
- `boxProjection`：启用盒体投影修正，默认 true。
- `roughness`：三个球体的粗糙度，0–1，默认 0.12。
- `intensity`：反射强度，0–2，默认 1。
- `position`：探针世界坐标，默认 `[0, 0.6, 0]`。
- `boxMin` / `boxMax`：世界空间轴对齐影响盒，默认 `[-4, -1, -4]`／`[4, 3, 4]`。探针必须位于盒内；坐标限制 ±20，每轴跨度至少 0.25。
- `blendDistance`：盒内边界与全局环境的混合距离，0–2，默认 0；盒外始终使用环境反射。
- `debug`：C++ 为 `ProbeDebug::Composite`／`Reflections`／`Influence`／`Cubemap`，网页为 `composite`／`reflections`／`influence`／`cubemap`。单探针影响图绿色为局部探针、红色为环境；多探针按红／绿／蓝／黄显示各探针归一化权重，灰色表示环境。

## 多探针

`SetReflectionProbes(volumes)`／`setReflectionProbes(volumes)` 设置最多四个 `ReflectionProbeVolume`；`GetReflectionProbes()`／`getReflectionProbes()` 返回按值复制的列表。也可直接设置 `ReflectionProbeSettings.probes`。空列表恢复旧的单探针字段，保留兼容；第十四个例子默认创建三个探针。

每个区域包含 `enabled`、`position`、`boxMin`／`boxMax`（盒体投影边界）、`influenceMin`／`influenceMax`（影响边界）、`intensity` 和 `blendDistance`。投影边界与影响区域分开定义：房间投影可以保持一致，影响区域可以重叠。默认区域位置和投影盒与单探针一致，影响盒默认也是 `[-4, -1, -4]`／`[4, 3, 4]`，强度 1、混合距离 0.8；位置必须在投影盒内。

```cpp
render::ReflectionProbeVolume left, right;
left.position = {-2, 0.6f, 0};
right.position = {2, 0.6f, 0};
left.influenceMax[0] = 1;
right.influenceMin[0] = -1;
view.SetReflectionProbes({left, right});
```

```js
view.setReflectionProbes([
  { position: [-2, 0.6, 0], influenceMax: [1, 3, 4] },
  { position: [2, 0.6, 0], influenceMin: [-1, -1, -4] },
])
```

`ReflectionProbeSettings.smoothBlending` 默认 true。每个探针在自身影响边界内按 `blendDistance` 做 smoothstep 淡入，重叠时归一化权重，避免数量增加导致反射变亮；权重不足部分补全局环境。关闭平滑混合可观察硬边界。强度先逐探针应用，再应用全局 `intensity`。`debugProbe` 选择 Cubemap 调试图的探针索引，默认 0；索引超出当前列表时拒绝设置。

移动探针只重建对应 Cubemap；切换环境或调用 `UpdateProbe()`／`updateProbe()` 会在下次绘制重建全部捕获。`UpdateProbe(index)`／`updateProbe(index)` 可只刷新一个探针；相机、AA、分辨率、粗糙度、强度和盒体参数变化会复用缓存。`GetProbeCaptureCount()`／`getProbeCaptureCount()` 返回实际完成的捕获次数。非法设置抛出异常并保留原值。

沿用相机、时间、环境加载、六档 AA、后处理与历史重置接口，命名与 [SSR](./ssr) 一致。性能条包含探针重建（仅需要时）、主场景、反射合成、AA、后处理和等待；原生还包含图像读回，不等于纯 GPU 耗时。

当前探针捕获的是内置静态程序化房间与立柱：共享 HLSL 解析求交生成六个 90° 方向的线性 HDR 图像，并以 64 个 GGX 重要性样本生成粗糙度层级。逻辑 Cubemap 六面以二维图集存储，每面 128×128、共八档粗糙度；探针元数据单独用半精度纹理存储，保持跨后端一致；尚未支持任意 GLB 场景捕获。反射球和位置标记不参与捕获，避免自身与递归反射。

盒体投影让反射射线与包围盒相交，再从探针位置指向交点采样 Cubemap。它改善近似房间墙面的视差，不能恢复真实遮挡、立柱精确视差或球体间反射；高粗糙度仍是预过滤近似。当前最多四个探针、轴对齐盒，尚未支持旋转盒体。
