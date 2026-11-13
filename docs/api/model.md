# 模型与相机

C++ 与网页使用同一组概念：加载模型、设置相机、绘制。底层 GLB 解析、纹理上传、深度缓冲和图形 API 均封装在内部。

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

## 鼠标操作

启用后，左键拖动旋转、滚轮缩放。C++ 使用 `Window::EnableOrbitControls()`，传 `false` 可关闭；事件由 `PollEvents()` 处理，继续通过主循环绘制。`GetCamera()` 返回当前相机副本。

网页使用 `renderer.enableOrbitControls({ onChange, onError })`，内部处理指针捕获、滚轮单位、边界和自动重绘。`onChange(camera)` 用于同步滑块，`onError(error)` 用于显示异常；可选 `rotateSpeed`（默认 0.35 度/像素）、`zoomSpeed`（默认 0.001）均须为正有限数。`disableOrbitControls()` 或返回的清理函数可关闭操作，`dispose()` 自动清理事件和待执行帧。重复启用不会叠加监听器。

滚轮仅在画布上接管，画布外正常滚动页面。网页 `getCamera()` 返回副本；程序调用 `setCamera()` 后仍需 `drawFrame()`。鼠标交互自动安排绘制，与其他 `drawFrame()` 调用在内部串行执行。

## 本阶段范围

支持内嵌资源的静态、单材质、不透明、索引三角形 GLB，要求 POSITION、NORMAL、TEXCOORD_0；首个例子为 Damaged Helmet。尚不支持蒙皮、动画、morph、透明材质、压缩扩展或通用多材质场景。内部将索引展开为统一顶点流。

材质包含基础颜色、法线、金属度/粗糙度、遮蔽与自发光贴图，采用 GGX 直接光照和解析式摄影棚环境近似；目前没有 HDR 环境贴图、预计算 IBL 或阴影。不是完整 glTF 一致性实现。

唯一维护的材质源码是 `render/shaders/model.hlsl`。原生端构建生成 DXIL / SPIR-V / metallib，网页端经 DXC + Naga 生成 WGSL；网页输出额外编码 sRGB。缺少贴图时使用内置默认纹理，缺少模型或不支持的格式会报错。

模型版权声明保留在 `assets/DamagedHelmet/`；资源许可与库代码分开。部署示例需携带 GLB，模型资源不内置进渲染库。

已验证：macOS Metal / Vulkan、Ubuntu Lavapipe，以及 Windows RTX 4060 的 DX12 / Vulkan 与 WARP。网页已检查实际画面及相机交互。Windows 渲染测试需要交互桌面会话，SSH 本身仅用于构建与调度。
