# 模型与相机

C++ 与网页使用同一组概念：加载模型、设置相机、绘制。默认只显示基础颜色贴图 × 材质基础颜色因子，不受光照、遮蔽、自发光或曝光影响。底层 GLB 解析、纹理上传、深度缓冲和图形 API 均封装在内部。

## C++

```cpp
render::Window window;
window.LoadModel("assets/DamagedHelmet/DamagedHelmet.glb");
render::OrbitCamera camera;
camera.yaw = 20;
camera.pitch = 5;
window.SetCamera(camera);
window.EnableOrbitControls();
while (window.PollEvents()) window.DrawFrame();
window.WaitIdle();
```

`Renderer` 同样提供 `LoadModel(path)` / `SetCamera(camera)`，供自行管理窗口的集成使用。未加载模型时仍绘制原三角形；加载失败抛异常，示例退出并报告错误。

```sh
cmake --build build --target nova_helmet
./build/nova_helmet assets/DamagedHelmet/DamagedHelmet.glb --api metal
# 其他可选后端：--api vulkan / --api dx12；Windows CPU 验证加 --warp
# 自动检查：--frames 60 --spin
```

## 网页

```js
const renderer = await Nova.createRenderer({ canvas, shaderDirectory: '/generated/' })
await renderer.loadModel('/generated/DamagedHelmet.glb')
renderer.setCamera({ yaw: 20, pitch: 5, distance: 3.6, exposure: 1 })
await renderer.drawFrame()
renderer.enableOrbitControls()
// 页面退出时 renderer.dispose()
```

网页模型 shader 来自构建时生成的 `model.json`；运行时不需要 DXC、Rust 或编译服务。网页滑块和 C++ `OrbitCamera` 的角度均为度，默认值为 `yaw=0`、`pitch=0`、`distance=3.6`、`exposure=1`。俯仰限制 ±85°、距离 1.5～100、曝光大于 0 且不超过 16。模型按包围盒居中并将最长边归一化到 2，相机环绕此中心。

## 光照

[模型例子](../examples/damaged-helmet) 使用默认的无光照模式；[光照例子](../examples/lighting) 显式开启 PBR。

```cpp
window.SetLighting(render::LightingMode::PBR); // Renderer 同样提供
render::DirectionalLight light;
light.direction = {-1, 1.6f, 2}; // 表面指向光源，世界坐标
light.color = {1, 0.9f, 0.8f};  // 线性 RGB
light.intensity = 3.5f;
window.SetDirectionalLight(light); // Renderer 同样提供
// GetDirectionalLight() 返回副本；SetLighting(Unlit) 恢复基础颜色
```

```js
renderer.setLighting('pbr') // 恢复基础颜色：'unlit'
renderer.setDirectionalLight({ direction: [-1, 1.6, 2], color: [1, 0.9, 0.8], intensity: 3.5 })
// getDirectionalLight() 返回深拷贝，修改后需 setDirectionalLight(light)
await renderer.drawFrame()
```

原生光照例子：`./build/nova_lighting assets/DamagedHelmet/DamagedHelmet.glb --api metal`。命令行可加 `--light-direction X Y Z --light-color R G B --light-intensity N`。模式只影响模型，保留相机与鼠标操作；无效模式会报错。主方向光可在绘制前随时更新。方向必须为有限、非零向量（长度至少 1e-6），内部自动归一化；颜色为 0～1 的线性 RGB，强度为 0～100 的倍率，默认方向 `[-1,1.6,2]`、颜色 `[1,3.2/3.5,0.8]`、强度 `3.5`。设置无效参数会报错并保留原配置。强度 0 关闭主光，但固定辅助光、解析式环境反射和自发光仍保留；无光照模式忽略这些参数。曝光影响 PBR 模型和天空盒，无光照模型的基础颜色保持不变。

## 鼠标操作

启用后，左键拖动旋转、滚轮缩放。C++ 使用 `Window::EnableOrbitControls()`，传 `false` 可关闭；事件由 `PollEvents()` 处理，继续通过主循环绘制。`GetCamera()` 返回当前相机副本。

网页使用 `renderer.enableOrbitControls({ onChange, onError })`，内部处理指针捕获、滚轮单位、边界和自动重绘。`onChange(camera)` 用于同步滑块，`onError(error)` 用于显示异常；可选 `rotateSpeed`（默认 0.35 度/像素）、`zoomSpeed`（默认 0.001）均须为正有限数。`disableOrbitControls()` 或返回的清理函数可关闭操作，`dispose()` 自动清理事件和待执行帧。重复启用不会叠加监听器。

滚轮仅在画布上接管，画布外正常滚动页面。网页 `getCamera()` 返回副本；程序调用 `setCamera()` 后仍需 `drawFrame()`。鼠标交互自动安排绘制，与其他 `drawFrame()` 调用在内部串行执行。

## 本阶段范围

支持内嵌资源的单材质、不透明、索引三角形 GLB，要求 POSITION、NORMAL、TEXCOORD_0；首个例子为 Damaged Helmet。骨骼与动画支持见[骨骼动画](./animation)；尚不支持 morph、透明材质、压缩扩展或通用多材质场景。内部将索引展开为统一顶点流。

PBR 模式使用基础颜色、法线、金属度/粗糙度、遮蔽与自发光贴图，采用 GGX 直接光照和解析式摄影棚环境近似；加载[环境资源](./environment)后使用 HDR 环境光与预过滤反射；尚无阴影。不是完整 glTF 一致性实现。

唯一维护的材质源码是 `render/shaders/model.hlsl`。原生端构建生成 DXIL / SPIR-V / metallib，网页端经 DXC + Naga 生成 WGSL；网页输出额外编码 sRGB。缺少贴图时使用内置默认纹理，缺少模型或不支持的格式会报错。

模型版权声明保留在 `assets/DamagedHelmet/`；资源许可与库代码分开。部署示例需携带 GLB，模型资源不内置进渲染库。

此前 PBR 版本已验证：macOS Metal / Vulkan、Ubuntu Lavapipe，以及 Windows RTX 4060 的 DX12 / Vulkan 与 WARP。网页已检查实际画面及相机交互。Windows 渲染测试需要交互桌面会话，SSH 本身仅用于构建与调度。

本次基础颜色/光照拆分已验证 macOS Metal / Vulkan（17 项检查）与 Chrome WebGPU；网页还比较了曝光无关性及两种模式的实际像素。此次未重新运行 Windows / Linux。

示例命令行由 `examples/common/ModelOptions.hpp` 统一解析；未指定 API 时由 Nova 按平台选择。`--spin` 仅用于自动绕模型旋转验证，默认关闭；`--frames N` 在绘制 N 帧后退出，常规交互使用鼠标操作。

示例通过[参数面板](./ui)在渲染区域内调整相机与光照，外部页面只保留代码和预览操作。

模型加载、光照和天空盒三个例子均可选择 Damaged Helmet、Avocado、BoomBox 或 Cornell Box，切换保留相机与灯光参数。自定义资源列表见[参数面板](./ui#模型与环境选择)。

Cornell Box 为项目生成的康奈尔盒子风格场景，包含红绿墙、两个方块和顶部发光面板。正面查看建议 `yaw=0`、`pitch=0`、`distance=3.6`。场景沿用当前灯光与环境；发光面照明依靠路径反弹采样，不是测量数据的校准复刻。
