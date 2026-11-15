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
- `debug`：C++ 为 `ProbeDebug::Composite`／`Reflections`／`Influence`／`Cubemap`，网页为 `composite`／`reflections`／`influence`／`cubemap`。影响范围图绿色为局部探针，红色为环境反射。

移动探针、切换环境或调用 `UpdateProbe()`／`updateProbe()` 会在下次绘制重建捕获；相机、AA、分辨率、粗糙度、强度和盒体参数变化会复用缓存。`GetProbeCaptureCount()`／`getProbeCaptureCount()` 返回实际完成的捕获次数。非法设置抛出异常并保留原值。

沿用相机、时间、环境加载、六档 AA、后处理与历史重置接口，命名与 [SSR](./ssr) 一致。性能条包含探针重建（仅需要时）、主场景、反射合成、AA、后处理和等待；原生还包含图像读回，不等于纯 GPU 耗时。

当前探针捕获的是内置静态程序化房间与立柱：共享 HLSL 解析求交生成六个 90° 方向的线性 HDR 图像，并以 64 个 GGX 重要性样本生成粗糙度层级。逻辑 Cubemap 六面以二维图集存储，每面 128×128、共八档粗糙度，保持跨后端一致；尚未支持任意 GLB 场景捕获。反射球和黄色位置标记不参与捕获，避免自身与递归反射。

盒体投影让反射射线与包围盒相交，再从探针位置指向交点采样 Cubemap。它改善近似房间墙面的视差，不能恢复真实遮挡、立柱精确视差或球体间反射；高粗糙度仍是预过滤近似。当前为单探针、轴对齐盒，尚未支持多探针混合与旋转盒体。
