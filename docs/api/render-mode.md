# 渲染模式

原生 `Window` 和 `Renderer` 提供相同接口。默认使用光栅；加载模型后，可在参数面板的 **Renderer → Mode** 切换渐进路径追踪。

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
  --mode path --spp 1 --bounces 3 --scale 0.5
```

模型加载、光照、天空盒三个原生例子均支持这些选项。Windows 使用 `--api dx12`，Linux 使用 `--api vulkan`；Windows 可追加 `--warp` 验证软件光追。

## 后端与当前范围

DX12 使用 DXR 1.1 inline ray query；Vulkan 使用 acceleration structure 与 ray query 扩展；Metal 使用原生加速结构与 ray query。内核只维护 HLSL，由 DXC 生成 DXIL / SPIR-V，再由 SPIRV-Cross 生成 Metal shader。没有新增第三方依赖。

当前实现支持静态不透明模型、基础色/法线/金属度粗糙度/自发光贴图、方向光阴影和 HDR 环境、多次反弹与渐进累计。尚无降噪、透明材质、蒙皮和多材质支持；为减少亮点，单条路径辐射值上限为 64。

这是功能验证版本：计算结果每帧回读 CPU，再上传到显示后端，存在传输与同步开销。软件光追适合验证功能，不用于判断硬件光追性能。

网页端本阶段仍使用光栅。`getCapabilities().pathTracing` 返回 `false`，面板禁用路径追踪；`setRenderMode('path-tracing')` 会报告原因。

构建时可用 `-DNOVA_ENABLE_PATH_TRACING=OFF` 关闭原生光追。
