# 骨骼动画

`Window`、`Renderer` 和网页 Renderer 提供对应接口。`LoadModel` / `loadModel` 加载带 skin 与动画的 GLB 后，即可查询片段并控制播放。

```cpp
auto clips = window.GetAnimations(); // name、duration（秒）
auto state = window.GetAnimationState();
state.clip = 1;
state.time = 0;
state.speed = 1;
state.playing = true;
state.loop = true;
window.SetAnimationState(state);

// 窗口循环中，传入距上一帧的实际秒数。
window.UpdateAnimation(deltaSeconds);
window.DrawFrame();
```

```js
const clips = renderer.getAnimations()
renderer.setAnimationState({
  clip: 1, time: 0, speed: 1, playing: true, loop: true,
})
await renderer.drawFrame()
```

网页首次绘制后自动播放，可在面板中选择片段、暂停、循环、调整速度和时间。预览不可见或浏览器切到后台时暂停推进；恢复后继续，不补算隐藏期间的时间。原生应用使用 `UpdateAnimation(deltaSeconds)` 推进；网页也提供 `updateAnimation(deltaSeconds)`，手动逐帧查看时，暂停播放并通过状态直接设置 `time` 即可，通常无需另外调用网页的 `updateAnimation`。

- `GetAnimations()` / `getAnimations()`：返回片段列表；静态模型为空。
- `GetAnimationState()` / `getAnimationState()`：返回状态副本。
- `SetAnimationState(state)` / `setAnimationState(state)`：设置片段、时间、速度、播放和循环。片段索引从 0 开始，时间为 `0–duration`，速度为 `0.1–4`；非法参数抛异常。切换片段时建议将时间设为 0。
- `UpdateAnimation(deltaSeconds)` / `updateAnimation(deltaSeconds)`：接受非负、有限的秒数。暂停时不推进；不循环时在结尾停止。

## 当前范围

按 [glTF 2.0 skin 与动画定义](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#skins) 处理关节层级、逆绑定矩阵和每顶点最多四个权重。支持平移、旋转、缩放通道的 LINEAR / STEP 采样，旋转使用四元数插值；权重可为 float 或归一化 uint8 / uint16，关节索引为 uint8 / uint16。

CPU 负责关键帧采样和骨骼/节点矩阵计算，共享 HLSL 顶点着色器负责位置与法线的 GPU 蒙皮。绑定姿态顶点、关节索引和权重只在加载时上传，每帧更新矩阵调色板与原有场景常量，材质和纹理不随帧重建。Metal、DX12、Vulkan 与网页使用同一段 HLSL；无新增依赖、无 WASM。仍限不透明单材质、内嵌资源的 GLB，不支持 morph、CUBICSPLINE、动画混合或压缩扩展；变换须可逆。

动画模型暂不支持路径追踪：能力查询返回 `pathTracing=false`，面板禁用此模式；切换调用会报告原因。静态模型的路径追踪与降噪不受影响。


## GPU 蒙皮验证

运行时不再逐顶点执行 CPU 蒙皮，也不逐帧上传变形顶点。法线使用混合蒙皮矩阵的逆转置变换，支持非均匀缩放。此示例的每帧动画数据由约 25.3 KiB 顶点降为 1.5 KiB 矩阵，播放 API 与 UI 不变。

独立 CPU 参考计算仅用于测试。可选 Metal 读回工具与网页验证通过共享 `skinVertex` 函数检查位置、法线、混合权重和多个姿态；使用方法见仓库 `tools/gpu_skinning/README.md`。
