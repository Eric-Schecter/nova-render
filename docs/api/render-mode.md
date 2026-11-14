# 渲染模式

原生 `Window`、`Renderer` 和网页 Renderer 提供对应接口。默认使用光栅；加载模型后，可在参数面板的 **Renderer → Mode** 切换渐进路径追踪。

```cpp
render::Window window;
window.LoadModel("assets/DamagedHelmet/DamagedHelmet.glb");
window.LoadEnvironment("assets/Environment/venice_sunset.nenv");

const auto capabilities = window.GetCapabilities();
if (capabilities.pathTracing)
{
    render::PathTracingSettings settings;
    settings.samplesPerFrame = 1;
    settings.maxBounces = 3;
    settings.resolutionScale = 0.5f;
    settings.lightSampling = true; // 默认开启光源重要性采样与 MIS
    window.SetPathTracing(settings);
    window.SetRenderMode(render::RenderMode::PathTracing);
}
window.EnableParameterPanel();
while (window.PollEvents())
    window.DrawFrame();
window.WaitIdle();
```

请将上述设置放入现有窗口示例的初始化部分；窗口构造和运行方式见 [Window](./window)。

## 接口

- `GetCapabilities()`：返回 `pathTracing`、`softwareRayTracing`、设备名称 `rayTracingDevice` 和不可用原因 `reason`。
- `SetRenderMode(RenderMode::Raster / PathTracing)` / `GetRenderMode()`：切换或读取模式。不支持光追或未加载模型时，切换抛异常并保留原模式。
- `SetPathTracing(settings)` / `GetPathTracing()`：每帧采样数 `1–4`、最大反弹次数 `1–8`、内部渲染比例 `0.25–1`。非法参数抛异常。
- `GetAccumulatedSamples()`：读取当前累计采样数。
- `ResetAccumulation()`：清空累计采样；下一帧重新开始。

相机、模型、光照、环境、背景色或渲染尺寸变化会重新累计。面板内可以调整参数、查看采样数和手动重置。切回光栅后恢复原有光栅材质与设置。

## 运行例子

```sh
./build/nova_environment assets/DamagedHelmet/DamagedHelmet.glb \
  assets/Environment/venice_sunset.nenv --api metal \
  --mode path --spp 1 --bounces 3 --scale 0.5 --integrator path
```

模型加载、光照、天空盒三个原生例子均支持这些选项。Windows 使用 `--api dx12`，Linux 使用 `--api vulkan`；Windows 可追加 `--warp` 验证软件光追。

## 后端与当前范围

DX12 使用 DXR 1.1 inline ray query；Vulkan 使用 acceleration structure 与 ray query 扩展；Metal 使用原生加速结构与 ray query。内核只维护 HLSL，由 DXC 生成 DXIL / SPIR-V，再由 SPIRV-Cross 生成 Metal shader。没有新增第三方依赖。

当前实现支持静态不透明模型、基础色/法线/金属度粗糙度/自发光贴图、方向光阴影和 HDR 环境、多次反弹与渐进累计。支持可选空间降噪与光源重要性采样 / MIS；尚无透明材质、蒙皮和多材质支持；为减少亮点，单条路径辐射值上限为 64。

原生当前为功能验证版本：计算结果每帧回读 CPU，再上传到显示后端，存在传输与同步开销。网页的计算与显示直接留在 GPU，不需要逐帧回读。

## 网页用法

```js
await renderer.loadModel('/generated/DamagedHelmet.glb')
renderer.setPathTracing({
  samplesPerFrame: 1,
  maxBounces: 3,
  resolutionScale: 0.5,
  lightSampling: true, // 默认值
})
renderer.setRenderMode('path-tracing') // 'raster' 切回光栅
await renderer.drawFrame()
renderer.enableParameterPanel()
```

对应方法为 `getCapabilities()`、`setRenderMode()` / `getRenderMode()`、`setPathTracing()` / `getPathTracing()`、`resetAccumulation()` 和 `getAccumulatedSamples()`，参数范围与原生一致。设置无效参数或未加载模型就切换路径追踪会抛异常。

网页在 JavaScript 中构建 BVH，使用 WebGPU compute shader 遍历与求交，不调用专用硬件光追接口。`softwareRayTracing: true` 在网页表示通用 GPU 上的 BVH 求交，并不表示 CPU 渲染；`rayTracingDevice` 为 `WebGPU compute BVH`。材质、光照和采样与原生共享 HLSL，仅求交实现不同。没有新增第三方依赖或 WASM。

首次 `drawFrame()` 后自动逐帧累计；隐藏预览、移出屏幕或切到后台时暂停，恢复可见后继续。切回光栅或 `dispose()` 会停止累计。普通 `drawFrame()` 仍可手动调用。面板提供模式、采样、反弹次数、渲染比例和重置按钮；低分辨率通过 GPU 双线性放大显示，效果仍需逐步收敛。

资源与渲染缓冲区须符合浏览器设备的 storage buffer 限制；超限会报告错误。性能取决于模型、分辨率和 GPU。

构建时可用 `-DNOVA_ENABLE_PATH_TRACING=OFF` 关闭原生光追。


## 光源采样与 MIS

`PathTracingSettings::lightSampling` 与网页 `lightSampling` 对应，默认 `true`。面板的 **Light sampling / MIS** 可切换；修改会重新累计。

发光三角形按面积与发光亮度选择，HDR 环境按亮度与立体角采样，并发射阴影射线检查遮挡。光源采样与原有 diffuse/GGX BSDF 采样通过 power heuristic 合并；直接看见灯或天空的亮度不变。关闭后使用 BSDF 采样，可用于同采样数对比。方向光仍单独采样。

康奈尔盒子建议正面相机、关闭方向光和环境照明，仅靠顶灯观察间接光与阴影；比较时关闭降噪。MIS 减少低采样噪声，但每次反弹增加一次光源采样及可能的阴影射线，不保证每帧更快。

## 双向路径追踪

面板 **Integrator** 可选择 Path tracing / Bidirectional，默认 Path tracing。两端通过 `integrator` 指定，切换会重新累计；降噪和反弹次数仍适用。

```cpp
auto settings = window.GetPathTracing();
settings.integrator = render::PathIntegrator::Bidirectional;
window.SetPathTracing(settings);
```

```js
renderer.setPathTracing({ ...renderer.getPathTracing(), integrator: 'bidirectional' })
// integrator: 'path' 切回单向路径追踪
```

原生例子也可使用 `--mode path --integrator bidirectional`。BDPT 从发光面生成光路，与相机子路径连接，以面积度量的路径概率和 power heuristic 合并各连接策略。两端共用 HLSL，材质与法线求值一致；BDPT 始终启用 MIS，`lightSampling` 开关仅用于单向 PT。

当前光路起点限于有限面光源；HDR 与方向光保留单向 NEE/MIS，没有面光源时回退到此估计器。不做光路直接投向其他像素的相机 splat；仍只支持静态不透明材质。算法背景可参阅 [PBRT 双向路径追踪](https://pbr-book.org/3ed-2018/Light_Transport_III_Bidirectional_Methods/Bidirectional_Path_Tracing)。

Cornell 测试关闭方向光、环境照明与降噪，48×48、5 次反弹，以 2048 spp PT 为参考：8 spp 线性亮度误差约降低 76%，32 spp 约降低 67%，高采样平均亮度差约 0.19%。BDPT 的光路和连接增加计算量；本场景里 PT 128 spp 的误差低于 BDPT 32 spp，不能仅凭同采样数更干净判断速度更快。

Windows WARP 软件验证使用微软 WARP 1.0.22；测试机器自带的旧 WARP 会在此复杂内核中崩溃。新版驱动仅放在测试程序旁，不修改系统 DLL，不增加项目依赖；实体 GPU 使用其 DX12 驱动。

## 降噪

路径追踪参数新增 `denoise`，默认 `false`。原生与网页面板的 **Denoise** 复选框控制同一功能，仅在路径追踪模式显示。

```cpp
auto settings = window.GetPathTracing();
settings.denoise = true;
window.SetPathTracing(settings);
```

```js
renderer.setPathTracing({ ...renderer.getPathTracing(), denoise: true })
await renderer.drawFrame()
```

两端共用 HLSL 的 5×5 深度、法线与亮度引导双边滤波，保护轮廓和表面边缘，天空背景不参与过滤。降噪在 GPU 的独立计算 pass 执行，只生成显示图，不回写原始辐射累计。修改开关与其他路径追踪参数一样会重新累计；关闭后恢复未过滤的采样结果。

这是轻量空间降噪，不使用 AI 或时域历史，没有新增依赖。极低采样下仍可能有亮点，细小纹理也可能变软；仍建议保持渐进累计。它不会补足较低内部渲染比例丢失的细节。

骨骼动画模型目前仅用光栅渲染，能力查询和面板会明确报告；动画接口见[骨骼动画](./animation)。
