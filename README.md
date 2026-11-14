# Nova Triangle

C++17 桌面渲染示例：通过 `render` 模块选择 Vulkan、DX12 或 Metal，以三角形和 Damaged Helmet 验证统一接口及 HLSL 材质。

## 平台与选择

- macOS：默认原生 Metal，可用 `--api vulkan` 切换 MoltenVK。
- Windows：默认 DX12，可用 `--api vulkan` 切换 Vulkan。
- Linux：Vulkan。
- `--api auto|vulkan|metal|dx12` 显式选择；不支持的组合直接报错，不静默切换。

`Auto` 是固定的平台映射，不是逐个尝试后端的优先级列表：macOS → Metal、Windows → DX12、Linux → Vulkan。显式请求不受支持或未编译的 API 会在创建窗口前报错；设备、shader 或首帧初始化失败直接报告原 API 的错误，不尝试其他 API。

`ResolveGraphicsAPI(config)` 统一执行选择与参数检查；`IsGraphicsAPIAvailable(api)` 只表示当前构建包含该后端，不保证驱动/设备可用。公共头文件不包含图形 API 类型。main 负责窗口、事件、参数和验证流程。

## 依赖和构建

- CMake 3.24+、C++17 编译器和本机构建工具。
- vcpkg manifest 固定 baseline；基础依赖为 `glfw3`，默认 `vulkan` feature 加入 `volk`（包含 Vulkan headers）。Windows / Linux x64 的 host DXC 使用 `directx-dxc` 包，macOS 的 host SPIRV-Cross 使用 `spirv-cross` 包。
- macOS 使用 Xcode Command Line Tools / Xcode 和系统 Metal、QuartzCore、Cocoa framework。Metal 后端内部使用 Objective-C++ 和 ARC，公共接口及示例仍为 C++。
- Windows 使用 MSVC 和 Windows 10/11 SDK，系统 SDK 提供 `d3d12`、`dxgi`；DXC 离线生成 DXIL，设备要求 Shader Model 6.0 / DXIL。
- Vulkan 运行时由显卡驱动提供；macOS 的 Vulkan 后端需要 Vulkan SDK / MoltenVK。原生 Metal 运行无需 Vulkan 运行时。

默认构建 Vulkan 后端及其 vcpkg feature。macOS / Windows 可使用 `-DNOVA_ENABLE_VULKAN=OFF` 仅构建原生后端，仍需要相应的 host shader 工具；Linux 仍要求 Vulkan。

```sh
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset debug
cmake --build --preset debug
```

macOS：

```sh
./build/nova_triangle.app/Contents/MacOS/nova_triangle --api metal
./build/nova_triangle.app/Contents/MacOS/nova_triangle --api vulkan
```

Linux：`./build/nova_triangle --api vulkan`。

Windows PowerShell：

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
cmake --preset windows-dx12
cmake --build --preset windows-dx12
 .\build-windows\nova_triangle.exe --api dx12
```

macOS 如果 SDK 未配置为系统 Vulkan 运行时，可在运行 Vulkan 后端前设置：

```sh
export VULKAN_SDK="$HOME/VulkanSDK/1.4.328.1/macOS"
export DYLD_LIBRARY_PATH="$VULKAN_SDK/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
export VK_ICD_FILENAMES="$VULKAN_SDK/share/vulkan/icd.d/MoltenVK_icd.json"
export VK_LAYER_PATH="$VULKAN_SDK/share/vulkan/explicit_layer.d"
```

## 验证

```sh
MTL_DEBUG_LAYER=1 ./build/nova_triangle.app/Contents/MacOS/nova_triangle --api metal --frames 120 --resize-test
./build/nova_triangle.app/Contents/MacOS/nova_triangle --api vulkan --frames 120 --resize-test
```

将上述后端和程序路径替换成对应平台即可复用检查。绘制满 20、40 帧后各改变一次窗口尺寸；60 帧后最小化、验证 `Resize(0, 0)` 返回 `Skipped` 并恢复，120 帧后退出。仅成功提交并安排呈现的帧计数；这是运行烟雾检查，不能替代图像正确性或屏幕实际扫描输出验证。手动可用 Esc 退出。

- Vulkan：`RendererConfig::enableValidation` 控制可用的 Khronos Validation Layer，错误通过 debug messenger 计数；缺少校验层时提示后继续。
- DX12：同一配置控制 D3D12 Debug Layer，需安装 Windows 可选功能 Graphics Tools；计数来自设备 InfoQueue 的 error / corruption 消息。
- Metal：异步 command buffer 错误在等待时检查并计数；Metal API Validation 使用进程环境变量 `MTL_DEBUG_LAYER=1` 或 Xcode Scheme 开启，`enableValidation` 不控制该系统层。API Validation 的系统日志与命令错误计数不是相同指标。
- 示例默认 Debug 请求校验、Release 不请求校验。运行失败或捕获到计数错误返回非零。

## 结构与接口

- `render/include/render/Renderer.hpp`：`GraphicsAPI`、`RendererConfig`、`Resize / DrawFrame / WaitIdle` 等公共接口。
- `render/src/Renderer.cpp`：平台默认选择和转发。
- `render/src/Backend.hpp`：模块内部后端契约。
- `render/src/backends/VulkanBackend.cpp`：Vulkan 生命周期与三角形绘制。
- `render/src/backends/MetalBackend.mm`：原生 Metal、CAMetalLayer 与三角形绘制。
- `render/src/backends/D3D12Backend.cpp`：原生 D3D12、DXGI swapchain、命令与 fence。
- `render/shaders/triangle.hlsl`：唯一手工维护的 shader，包含 `VSMain` / `PSMain`。
- `cmake/Shaders.cmake`：各后端的离线 shader 构建规则。
- `examples/triangle/main.cpp`：窗口、事件、后端选择与验证用例。

构建时按已启用的后端生成 `build/shaders/` 下的产物，运行时只加载二进制：

- DX12：HLSL → DXC (`vs_6_0` / `ps_6_0`) → `triangle.vert.dxil` / `triangle.frag.dxil`。
- Vulkan：HLSL → DXC (`-spirv -fspv-target-env=vulkan1.0`) → `triangle.vert.spv` / `triangle.frag.spv`，入口保留 `VSMain` / `PSMain`。
- Metal：HLSL → DXC → 独立 SPIR-V → SPIRV-Cross → MSL 2.1 → `xcrun metal` → AIR → `xcrun metallib` → `triangle.metallib`。

Vulkan 顶点编译使用 `-fvk-invert-y` 配合正高度 viewport；Metal 使用未翻转的独立 SPIR-V。MSL、SPIR-V、DXIL、AIR 和 metallib 都是构建产物，不手工维护或提交。修改 HLSL 会触发所需产物重新生成，编译失败会阻止应用构建。

CMake 从 vcpkg host 工具目录、`VULKAN_SDK/bin`、macOS 的 `~/VulkanSDK/*/macOS/bin` 和 PATH 查找工具。可以通过 `-DNOVA_DXC_EXECUTABLE=/path/to/dxc`、`-DNOVA_SPIRV_CROSS_EXECUTABLE=/path/to/spirv-cross` 显式覆盖。macOS 需要 Vulkan SDK 的 DXC 和 Xcode Metal Toolchain；缺少后者时运行 `xcodebuild -downloadComponent MetalToolchain`。

固定 vcpkg 基线的 DXC 包不支持 macOS / Linux ARM64。Linux ARM64 需另行提供可运行的 DXC，例如从官方源码构建。本次 Ubuntu 验证使用隔离的官方 Linux x64 DXC 1.8.2505.1 和 QEMU 用户态工具仿真；只有构建工具经仿真，渲染程序和 Lavapipe 仍为原生 ARM64。不复制预编译 SPIR-V 来绕过构建验证。

`Renderer(window, config)` 借用 GLFW 窗口；窗口和 GLFW 必须比 Renderer 活得更久。shaderDirectory 留空使用内置资源，非空则从指定目录加载；在窗口主线程使用，当前只支持同时存在一个 Renderer（Vulkan 的 volk 全局派发表限制）。初始化失败自动清理已创建资源，运行错误抛异常。

应用在首次绘制及尺寸变化后传入 framebuffer 像素尺寸。零尺寸暂停绘制，重建延迟到 `DrawFrame()`；模块不创建窗口、不处理事件、不等待窗口恢复。各后端使用单个 CPU 在途帧，优先保证清晰和正确性，暂不做吞吐优化。

退出前调用 `WaitIdle()` 并检查 `ValidationErrorCount()`，随后按 Renderer → Window → GLFW 销毁。析构仍负责等待与清理；销毁阶段的日志不计入示例退出前读取的计数。

支持 CMake 安装包与 `Nova::Render` 导出，默认 shader 已内置。没有引入 ECS、RenderGraph 或原 nova 的复杂模块。

## 当前验证状态（HLSL 离线构建，本机 ARM64 环境）

- macOS：完整构建 12 项、关闭 Vulkan 的原生构建 11 项，全部通过。Metal API Validation 和 Vulkan Khronos Validation Layer 已启用。
- Windows 11 ARM64：完整构建 9/12 项通过，DX12-only 构建 8/11 项通过。两种构建的 WARP 实际绘制、缩放和最小化恢复均通过，D3D12 Debug Layer 开启。每种构建各有 3 项普通 DX12 / Auto 测试失败：Parallels 虚拟显卡仅支持 DXBC，不支持 DXC 生成的 DXIL。程序检查 Shader Model 6.0 并明确拒绝，不静默改用 WARP。普通 GPU 路径需要支持 DXIL 的设备再验证。
- Windows Vulkan：使用现有 CLion/JCEF 附带的 ARM64 SwiftShader ICD，功能检查通过。此环境未安装 Khronos Validation Layer，日志明确显示 `Validation: disabled`，不将其计数 0 当作 API 校验通过。
- Ubuntu 24.04.3 ARM64：独立 X11/Openbox + Lavapipe 环境，11 项运行检查通过，Khronos Validation Layer 开启；另外验证了关闭 Vulkan 时配置阶段拒绝构建。

合计 58 项平台检查中 52 项通过、6 项因上述虚拟 GPU 能力不足失败，没有跳过或修改普通 DX12 的成功断言。检查覆盖隐式/显式 Auto、不可用/未编译 API、API/WARP 组合、启动失败不回退，以及成功路径的 120 帧、缩放、真实最小化和恢复。DX12 shader 资源缺失测试显式使用 WARP，将文件加载错误与 GPU 能力分开验证。

另行通过独立构建检查：干净生成全部 macOS shader、无修改不重复编译、HLSL 语法错误阻止构建、修复后重新生成，以及四份中间/最终 SPIR-V 的 `spirv-val --target-env vulkan1.0` 校验。实际查看了 Metal 窗口，三角形方向与颜色正确。

未验证 x64、Wayland 桌面或 NVIDIA / AMD / Intel 实机驱动与性能。Linux 本次是隔离显示环境中的自动检查，不是登录桌面的目视验收。

本次汇总与详细日志为 `build/validation/hlsl-*`，保留所选 API、驱动名和校验层状态，旧日志不作为新构建链的依据。构建产物和本机日志均被 Git 忽略。

## Windows / CLion 验证

CLion 可使用 MSVC Build Tools（无需完整 Visual Studio IDE）及 Windows SDK。安装 ARM64 版本 CLion 时，配置 Toolchains 中的 Visual Studio/MSVC 工具链，选择 ARM64 编译环境；CMake/Ninja 使用 CLion bundled 工具。设置 `VCPKG_ROOT` 后选择 `windows-msvc` CMake preset（保留 Vulkan），或 `windows-dx12`（仅 DX12），构建目录分别为 `build-windows-vulkan`、`build-windows`。

运行配置的参数分别为：

```text
--api dx12 --frames 120 --resize-test
--api dx12 --warp --frames 120 --resize-test
```

`--warp` 显式使用微软软件适配器（CPU），仅对 DX12 有效。普通 DX12 模式只选择支持 D3D12 的硬件/虚拟适配器，不自动回退 WARP。日志会打印所选设备及类型，防止混淆验证范围。

可在 MSVC developer shell 中通过 `scripts/windows-validate.ps1` 复用同一套 CMake/Ninja/CTest 检查；默认验证 DX12-only，`-Vulkan` 验证完整构建。`-VulkanICD` 和 `-VulkanLoaderDirectory` 可指定软件驱动的运行环境，不是项目链接依赖。参数也支持显式指定 CLion 的 CMake、Ninja 和 Build Tools 的 `VsDevCmd.bat` 路径。Windows 项目应放在虚拟机本地磁盘，避免在 Mac 共享目录中构建。

本机 Windows 验证副本在 `C:\nova-validation\nova-v2`，CLion、Build Tools 和 vcpkg 位于 `C:\nova-validation` 下。仓库中的通用 preset 不写死这些路径；虚拟机内的 `CMakeUserPresets.json` 提供已配置并构建通过的 `vm-dx12-clion` preset。构建日志另存为 Mac 项目的 `build/windows-validation.log`。

## 自动检查与 Linux 环境

```sh
ctest --test-dir build --output-on-failure
```

CTest 注册的用例按平台和 `NOVA_ENABLE_VULKAN` 条件生成，串行执行，避免窗口操作互相干扰。`--shader-dir PATH` 可覆盖资源目录；测试用空目录检查启动失败及不回退行为。最小化检查等待窗口管理器确认状态，而不是使用固定延时。

Linux 构建需要 C++17、CMake、Ninja、Git，以及 GLFW 所需的 X11 开发包；构建可使用 `linux-vulkan` preset。运行时需要 Vulkan Loader 和可用 ICD。Lavapipe、Validation Layers、Xvfb、Openbox 都属于系统运行/测试环境，不加入项目的 vcpkg 依赖。

本次在隔离 X11 环境启动 Openbox 后执行 CTest，并通过 `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json` 显式选择 Lavapipe。使用虚拟显示器时也需要窗口管理器，否则不能验证实际最小化/恢复。Linux 禁用 Vulkan 的配置必须失败：

```sh
cmake -S . -B build-no-api -DNOVA_ENABLE_VULKAN=OFF
```

## 接口文档网站

文档源码位于 `docs/`，独立使用 Node.js 22+ 与 VitePress，不影响 C++ / vcpkg 构建。

```sh
npm ci
npm run docs:dev
```

打开终端输出的本地地址（默认 `http://127.0.0.1:5173`）。包含快速开始、公共 API 参考和示例编写步骤，以及本地搜索和明暗主题。

构建与预览静态产物：

```sh
npm run docs:build
npm run docs:preview
```

默认静态预览地址为 `http://127.0.0.1:4173`；端口占用时以实际输出为准。产物位于 `docs/.vitepress/dist/`，不提交 Git。本阶段仅提供本地预览，尚未发布到公网。首次安装用 `npm ci` 复用锁文件中的版本。


### HLSL / WebGPU 交互预览

运行 `npm run docs:dev` 后打开 `/examples/triangle`：编辑 HLSL 并点击运行，由本机 DXC 和 Naga WASI 转为 WGSL，再通过 WebGPU 绘制。DXC 可用 `NOVA_DXC_EXECUTABLE` 指定；macOS 自动查找 Vulkan SDK。

`npm run docs:build` 自动生成静态示例资源。静态网站能展示默认三角形，但不提供即时 HLSL 编译。`npm run check:webgpu` 在运行中的本地预览上执行 Chrome GPU 像素读回检查；`node scripts/webgpu/check-ui.mjs` 检查编辑、错误和重置流程，需要本机 Chrome。

已在本机 Chrome 的 Apple / Metal 3 非回退适配器验证该链路。新增依赖仅用于文档与验证，不改变 C++ 后端或 vcpkg manifest。


## 安装为 C++ 依赖

```sh
cmake --preset debug -DNOVA_BUILD_EXAMPLES=OFF
cmake --build --preset debug
cmake --install build --prefix /absolute/path/to/nova-install
cmake -S examples/consumer -B build/consumer -DCMAKE_PREFIX_PATH=/absolute/path/to/nova-install -DCMAKE_BUILD_TYPE=Debug
cmake --build build/consumer
```

应用使用 `find_package(Nova 0.1 CONFIG REQUIRED)` 并链接 `Nova::Render`，推荐用 `render::Window` 创建窗口，应用不需要直接使用 GLFW。默认安装包含必要的 vcpkg GLFW / volk 开发文件；可用 `NOVA_BUNDLE_DEPENDENCIES=OFF` 改由使用方管理。使用安装包不需要 shader 编译器或 Node.js。

默认使用内置 shader，不必携带 shader 文件。显式 `--shader-dir` 仍可覆盖，路径错误会直接失败。静态 triplet 可减少动态库依赖；动态 triplet 仍需部署对应动态库。显卡驱动、平台图形组件和窗口系统仍是运行要求。包须匹配平台、架构和编译器 ABI。

独立示例见 `examples/consumer/`，网站说明见“作为依赖使用”。新安装流程已在 macOS ARM64 验证；Windows / Linux 包尚未验证。

推荐示例入口为 `render/Window.hpp`：`WindowConfig` 配置窗口，`Window::PollEvents()` / `DrawFrame()` 驱动主循环，窗口系统和尺寸同步由库内部管理。原 `Renderer(GLFWwindow*, ...)` 保留为底层接口，原三角形验证用例继续使用它检查窗口边界行为。

网页预览默认展示并执行 `web/examples/triangle.js`：通过 `web/src/Nova.js` 的 `createRenderer / setShader / setClearColor / drawFrame / dispose` 调用，无需直接使用 WebGPU。JavaScript 源码只读展示，背景设置实时生效；HLSL 标签保留本地编辑能力。


## Damaged Helmet 模型例子

```sh
cmake --build build --target nova_helmet
./build/nova_helmet assets/DamagedHelmet/DamagedHelmet.glb --api metal --frames 60
# macOS / Linux Vulkan: --api vulkan
# Windows DX12: --api dx12 (CPU validation: --warp)
```

接口：`Window::LoadModel(path)`、`SetCamera(OrbitCamera)`；网页对应 `loadModel(url)`、`setCamera(camera)`。两端均从 `render/shaders/model.hlsl` 生成材质，不维护额外 WGSL / MSL 源码。支持静态、单材质、不透明、内嵌资源的核心 GLB；使用基础颜色、法线、金属度/粗糙度、AO、自发光及解析式摄影棚光照，尚无 HDR IBL、阴影或动画。

网页例子：`/examples/damaged-helmet`，源码位于 `web/examples/helmet.js`；`node scripts/webgpu/check-model.mjs` 检查实际绘制及相机交互。原生数据、Metal、Vulkan 模型检查已加入 CTest。

TinyGLTF / GLM / stb / JSON 依赖均封装为库内部实现，不暴露到公共头文件。TinyGLTF 使用 vcpkg overlay 固定官方 v2.9.7 提交和文件哈希，以规避上游 release 归档元数据变化导致的旧哈希失败。使用安装包时无需重新安装这些头文件依赖。

模型 GLB 原样保存在 `assets/DamagedHelmet/`，来源、哈希和模型许可见该目录 README / UPSTREAM。模型资源与库代码许可分开，网页生成资源不提交 Git。

模型阶段验证（2026-11-13）：macOS ARM64 15 项检查通过（Metal / MoltenVK），Ubuntu ARM64 13 项通过（Lavapipe、Khronos 校验层）。Windows x64 RTX 4060 真机通过 15 项检查，并单独完成 DX12 模型 60 帧验证；D3D12 Debug Layer 与 Vulkan Khronos 层启用。Windows 图形检查在已登录桌面会话执行，SSH 服务会话不能创建 DX12 交换链。网页端已检查真实模型绘制、相机改变 GPU 输出、源码一致性、折叠、页面销毁，以及无需编译服务的静态预览。本机日志在 `build/validation/helmet-*`，不提交 Git。

Damaged Helmet 默认启用左键拖动旋转、滚轮缩放。网页入口为 `renderer.enableOrbitControls({ onChange, onError })`，自动合并重绘并同步相机；`disableOrbitControls()` / `dispose()` 清理监听器和待执行帧。C++ 使用 `Window::EnableOrbitControls()`，通过 `PollEvents()` 更新相机。两端可用 `GetCamera()` / `getCamera()` 读取副本，俯仰和距离均限制在有效范围。`node scripts/webgpu/check-orbit.mjs` 验证真实鼠标输入、GPU 输出、缩放边界、滑块同步及销毁清理。

### 模型与光照示例

`nova_helmet` 默认仅显示基础颜色贴图；`nova_lighting` 通过 `SetLighting(LightingMode::PBR)` 开启摄影棚光照。网页对应 `/examples/damaged-helmet` 和 `/examples/lighting`，使用 `setLighting('pbr')`。两种模式共享 HLSL 和鼠标操作封装，曝光仅影响 PBR。

光照例子可通过统一 `DirectionalLight` API 调整主光方向、线性 RGB 与强度；网页提供对应滑块。模型/光照例子的命令行解析集中于 `examples/common/ModelOptions.hpp`，`--spin` 为可选自动旋转验证模式。

### 代码格式

C++ / Objective-C++ / HLSL 使用项目根目录的 `.clang-format`（沿用 Nova 项目配置：4 空格、120 列、独立行花括号）。网页源码使用固定版本 Prettier：`npm run format:web`，检查使用 `npm run check:format:web`。

### 预览内参数 UI

模型与光照示例调用 `Window::EnableParameterPanel()`，在窗口内通过 ImGui 修改背景、相机与光照；网页对应 `renderer.enableParameterPanel()`，使用 HTML/CSS 浮层，无 WASM。两端参数与分组一致，UI 输入与模型操作隔离。`NOVA_ENABLE_UI=OFF` 可关闭原生 UI 构建；默认开启，vcpkg 管理依赖。详见 `docs/api/ui.md`。

本次 UI 验证：macOS 19 项、Ubuntu Lavapipe 15 项全部通过；原生实际鼠标操作和窗口缩放正常，网页桌面/手机面板与输入隔离通过。独立安装包的 Metal/Vulkan 示例均运行 120 帧且验证错误为 0，禁用 UI 构建通过。Windows SSH 连接超时，DX12 UI 尚未实机验证。

### 天空盒与环境光

第四个例子：原生 `nova_environment`，网页 `/examples/environment`。加载同一份 HDR 预处理资源，提供天空盒、漫反射环境光和 GGX 预过滤反射；参数面板调整旋转、环境强度、天空亮度与显隐，保留鼠标环绕与缩放。

```sh
cmake --build build --target nova_environment
./build/nova_environment assets/DamagedHelmet/DamagedHelmet.glb assets/Environment/venice_sunset.nenv --api metal
# Linux 使用 --api vulkan；Windows 使用 --api dx12，CPU 验证可加 --warp。
```

公共接口与资源准备见 `docs/api/environment.md`。资源来自 Poly Haven，来源与 CC0 许可见 `assets/Environment/README.md`。运行时不需要新的外部依赖；可选离线工具复用现有 stb / GLM。安装库后也可独立构建 `examples/environment`，资源路径由调用者传入。

网页检查：`node scripts/webgpu/check-environment.mjs`；原生 CTest 包含环境数据、绘制和资源重载检查。

环境阶段验证（2026-11-13）：macOS Metal / MoltenVK 24 项、Ubuntu Lavapipe 18 项、Windows ARM64 虚拟机 DX12 WARP 11 项通过；Windows 图形测试在已登录桌面执行，开启 D3D12 Debug Layer。WARP 验证不代表本轮已验证 Windows 真机 GPU。Chrome WebGPU 检查实际像素、四例面板、资源重载和静态站点；另验证安装包独立构建第四例及 `NOVA_ENABLE_UI=OFF`。本机日志在 `build/validation/environment-*`。

### 可切换的示例资源

三个模型例子均可选择 Damaged Helmet、Avocado、BoomBox；第四例另提供 Venice Sunset、Kloppenheim 06、Studio Small 09 三套环境。切换保留相机与灯光参数，资源按需加载。新增资源不引入依赖，声明集中在文档页脚入口和资源目录。

原生使用 `EnableParameterPanel(ParameterPanelConfig)`，网页使用 `enableParameterPanel({models, environments})`；应用提供显示名与资源地址，加载与替换封装在 Nova 内。详见 `docs/api/ui.md`。验证命令：`ctest --test-dir build --output-on-failure`、`node scripts/webgpu/check-asset-choices.mjs`。

资源切换验证（2026-11-13）：Mac Metal / MoltenVK 26 项、Ubuntu Lavapipe 19 项、Windows DX12 WARP 12 项通过；包含九种模型与环境组合。另以 Ubuntu 实际键鼠检查两个下拉框，以 Chrome 检查三个模型页面、像素变化、失败恢复、窄屏和静态站点，并验证安装包独立调用及无 UI 构建。Windows 本轮仅验证 WARP，未验证真机 GPU。本机日志与截图在 `build/validation/asset-choices-*`。

### HDR 背景画质

天空盒改用 4K HDR 原图生成的独立 512×512 六面 RGBA16F 立方体，带常规空间 mip 过滤；模型照明继续使用原有的 256×256 GGX 预过滤贴图和 SH 系数，数据保持不变。支持旧版环境资源，公开 API 不变。当前环境包约 20 MiB/套；4K 原图作为可选离线输入，不随仓库分发，下载地址、校验值与重建命令见 `assets/Environment/README.md`。

网页生成资源以 `.bin` 提供，使用标准二进制 MIME，避免本地静态服务器将 HDR 数据当作文本实时压缩；格式内容仍是 Nova 环境资源。`node scripts/webgpu/check-hdr-background.mjs` 检查同视角画面、照明像素不变、旧格式和错误头部。构建后在端口 4187 启动静态预览，运行 `node scripts/webgpu/check-hdr-static.mjs` 检查静态切换与响应类型。

背景升级验证（2026-11-13）：Mac 26 项、Ubuntu Lavapipe 19 项、Windows DX12 WARP 12 项通过；Chrome 检查三套环境切换、背景与照明独立、实际前后画面、缩小过滤和静态预览。安装包独立运行及无 UI 构建通过。本机日志在 `build/validation/hdr-*`；本轮没有验证 Windows 真机 GPU。

### 原生路径追踪

三个模型例子支持在预览面板 **Renderer → Mode** 切换光栅与渐进路径追踪，并设置每帧采样、反弹次数、内部渲染比例和重置累计。相机、模型、环境与光照变化自动重新累计。公开 API 和命令行示例见 [渲染模式](docs/api/render-mode.md)。继续只维护 HLSL，使用 DXC → DXIL / SPIR-V → SPIRV-Cross → Metal 的构建链，无新增第三方依赖。

本阶段验证（2026-11-13）：Apple M4 Metal 实际 GPU 通过光线命中与像素、渐进收敛、模式切换和九种资源组合；Mac 回归 30 项通过、2 项因 MoltenVK 无光追能力跳过。Ubuntu ARM64 Lavapipe 的 ray-query、path-trace、render-modes 三项通过；Windows ARM64 WARP 回归 15 项通过、虚拟显卡硬件 DXR 探测跳过。Windows RTX 真机尚未验证，软件测试不能代表硬件性能。探测工具和旧版 ARM Mesa 的进程级环境配置见 `tools/ray_probe/README.md`。

当前实现支持可选空间降噪，尚无透明和动画支持，计算结果通过 CPU 回读再上传显示，属于可运行的功能版本。网页现已支持 WebGPU compute BVH 路径追踪，见下节。可用 `NOVA_ENABLE_PATH_TRACING=OFF` 关闭原生光追。安装包独立调用 Metal 光追、无 UI 构建及关闭光追构建均已通过；本机验证记录在 `build/validation/path-*`。


### 网页 BVH 路径追踪

网页三个模型例子均可在 Renderer → Mode 切换光栅 / Path tracing。API 与原生对应，支持采样、反弹次数、内部渲染比例和手动重置；相机、模型、光照、环境与尺寸变化自动重新累计。隐藏或移出预览、切到后台暂停，恢复后继续。默认仍为光栅。

共享 `render/shaders/pathtrace.hlsl` 的材质、光照和采样逻辑，网页分支使用线程化 BVH 求交。JavaScript 构建 BVH，DXC → SPIR-V → Naga 在构建时生成 WGSL；计算、累计和显示直接留在 GPU，无 WASM、无新增依赖、无需服务器编译。浏览器路径使用通用 GPU 计算，不是 DXR/Metal 专用光追单元；支持可选空间降噪。

验证命令：`node scripts/webgpu/check-path-kernel.mjs`（命中、实际像素与 Helmet HDR 渐进收敛）和 `node scripts/webgpu/check-path-modes.mjs`（统一 API、九种资源组合、真实面板与暂停恢复）。静态站点启动后可设置 `NOVA_PREVIEW_URL=http://127.0.0.1:4187` 执行后者。网页依赖及运行限制见 `docs/api/render-mode.md`。

网页光追验证（2026-11-14）：Chrome WebGPU 通过 BVH 正反面、未命中、平行、近距离和边缘射线检查；Damaged Helmet 15,452 个三角形、8,191 个 BVH 节点实际运行，2 → 8 次采样相对 34 次参考图的 MSE 从 391.18 降至 79.84。通过红色三角形像素、累计与重置、九种资源组合、切回光栅像素恢复、隐藏暂停恢复及桌面/手机面板检查。静态站点三个例子全部通过且未请求在线编译；共享 HLSL 的原生 Metal 两项回归通过，原生 DXIL 分支编译通过。日志与截图在 `build/validation/web-path-*`。


### 路径追踪降噪

C++ `PathTracingSettings::denoise` 与网页 `setPathTracing({ ...settings, denoise: true })` 对应，默认关闭；两个预览面板都提供 Denoise 开关。共用 HLSL 的 5×5 深度/法线/亮度引导空间滤波，独立 GPU pass 只处理显示结果，不修改原始累计。修改开关重新累计，关闭恢复原始输出。无新增依赖，无 AI 或时域历史；低采样下可能保留亮点并柔化细节。详见 `docs/api/render-mode.md`。

降噪验证（2026-11-14）：Metal、Vulkan Lavapipe 和 DX12 WARP 均通过带贴图 Helmet 的实际像素、开关重置与原始输出恢复测试。以 34 次采样为参考，Metal 的 2 次采样模型区域 MSE 从 407.97 降至 194.05，Chrome 从 391.19 降至 214.45；检查天空像素不变。Mac 完整回归 30 项通过、2 项按能力跳过，Ubuntu 三项通过，Windows WARP 15 项通过、硬件探测跳过。浏览器三个例子及手机面板的开关通过；Metal API 校验、安装包独立调用与无 UI 构建通过。日志及原始/降噪截图在 `build/validation/denoise-*`。


### Cornell Box 场景

三个模型例子的 Model 列表新增 Cornell Box，原生和网页共用自包含的 GLB：红绿侧墙、白色房间、两个方块和顶部发光面板。支持光栅、路径追踪和降噪。切换保留当前相机与灯光，正面建议 yaw=0、pitch=0、distance=3.6。它是自行生成的示意场景，不是测量数据复刻；路径追踪默认使用发光面与 HDR 重要性采样、MIS；关闭方向光与环境照明也能靠顶灯观察间接光。

重建资源：`node scripts/generate-cornell-box.mjs`，无新增依赖。资源说明在 `assets/CornellBox/README.md`；`node scripts/webgpu/check-cornell.mjs` 验证实际墙面颜色、32 次采样与三个例子的列表/模式/降噪切换。

场景验证（2026-11-14）：Metal API 校验下加载并实际追踪康奈尔盒子，检查红绿墙像素与降噪输出；Chrome 同样检查 32 次采样结果，两个实现均生成可查看的截图。Mac 的资源选择、模式切换与路径追踪五项回归通过，网页四个模型 × 三套环境的 12 种组合通过。三个例子在开发及构建后的静态网站中均可选择 Cornell Box 并切换光栅、路径追踪和降噪。日志和截图在 `build/validation/cornell-*`。


### 05 · 骨骼动画

新增 `nova_animation` 原生例子与网页 `/examples/skeletal-animation`，默认使用 Khronos Fox（24 关节、576 三角形），提供 Survey / Walk / Run 片段；自建 Skinning Robot 保留为混合权重回归资源。面板控制片段、播放/暂停、循环、速度和时间，相机仍可拖动/缩放。

接口：`GetAnimations()`、`GetAnimationState()`、`SetAnimationState(state)`、`UpdateAnimation(deltaSeconds)`，网页使用对应小写方法。原生窗口循环传入每帧秒数；网页自动播放并在预览隐藏时暂停。CPU 采样骨骼矩阵，共享 HLSL 顶点着色器执行 GPU 蒙皮，每帧仅更新矩阵，不重建纹理；支持 glTF TRS LINEAR / STEP，不支持 CUBICSPLINE、morph 或动画混合，动画模型暂限光栅。没有新增依赖，原静态模型的路径追踪保持可用。

运行：`./build/nova_animation assets/Fox/Fox.glb --api metal`；Windows 使用 `--api dx12`，软件验证追加 `--warp`，Linux 使用 `--api vulkan`。小机器人回归资源可用 `node scripts/generate-skinning-demo.mjs` 重建；Fox 来源与许可保存在 `assets/Fox/`。接口说明见 `docs/api/animation.md`。

动画验证（2026-11-14）：原生与网页在 Walk 0.5 秒的完整顶点/法线数据最大差约 `1.2e-7`；检查了混合权重、不同片段、暂停、速度、时间定位、非循环结尾与非法参数。Mac 完整回归 33 项通过、2 项按能力跳过；Ubuntu Vulkan 的动画、数据及原有路径追踪五项通过；Windows DX12 WARP 17 项通过、硬件探测跳过。Chrome 验证实际蒙皮像素、静态站点第五例、控件、隐藏暂停恢复和手机布局；原生窗口截图检查后修正了控件标签裁切。另验证了从静态路径追踪切换到动画时清空累计、安装包独立调用及无 UI 构建。日志与截图在 `build/validation/animation-*`。


### GPU 蒙皮

第五例现使用共享 `model.hlsl::skinVertex` 在顶点着色器中进行 GPU 线性蒙皮。CPU 只采样关键帧、计算关节/节点矩阵；绑定顶点与权重为静态缓冲，每帧只上传矩阵和原有场景常量。C++ / 网页播放 API 与 UI 不变，动画模型仍限光栅。无新增依赖。

小机器人回归资源每帧动画数据由 25,920 字节顶点改为 1,536 字节矩阵，CPU 不再逐帧遍历顶点。仅测试保留 CPU 蒙皮参考：`NOVA_BUILD_SKINNING_PROBE=ON` 可运行 Metal GPU 读回，对比两片段、混合权重、非均匀/镜像缩放；网页检查 GPU 输出、实际像素及仅更新矩阵的上传行为。使用说明见 `tools/gpu_skinning/README.md`。

GPU 蒙皮验证（2026-11-14）：Metal 真实 GPU 读回 20 个姿态，含双关节混合与非均匀/镜像缩放，相对测试专用 CPU 参考最大误差 `1.79e-7`；网页 GPU 读回误差 `1.49e-7`，并验证绑定顶点不变、每帧只写矩阵和场景常量。Mac 36 项中 34 项通过、2 项按能力跳过；Ubuntu Vulkan 五项通过，Windows DX12 WARP 17 项通过、硬件探测跳过。静态网站验证不同动画时刻的实际像素、控件与窄屏/隐藏生命周期；原有静态路径追踪、资源切换、无 UI 构建和独立安装包调用均通过。日志及截图在 `build/validation/gpu-skin-*`。


### Fox 动画与环境

第五例替换为 Khronos 归档仓库的原始 Fox GLB，保留三段动画与 GPU 蒙皮。加载器新增非索引三角形、缺失法线支持，源 GLB 不改写。C++ 与网页均提供三套环境切换、环境光强度、旋转、天空盒强度及显隐，切换保留动画状态。原生可用 `--environment path.nenv` 覆盖默认 Venice Sunset；默认方向光为 0，由环境照亮模型。署名集中在文档页脚资源声明及 `assets/Fox/`。


Fox 与环境验证（2026-11-14）：原始 GLB 校验值不变，加载器验证 24 关节、576 三角形、三片段及生成法线；网页 GPU 姿态与原生参考核对通过。Mac 37 项中 35 项通过、2 项按能力跳过；Ubuntu 六项通过；Windows WARP 18 项通过、硬件探测跳过。验证了默认/自定义原生环境入口、三套环境保留动画状态、天空盒显隐与环境光分别影响像素、静态网站/手机控件、无 UI 构建及安装包独立调用。日志与截图在 `build/validation/fox-*`，第三方署名保存在资源目录及文档页脚。

### 光源重要性采样与 MIS

C++ `PathTracingSettings::lightSampling` 和网页 `lightSampling` 默认开启，两个面板均提供 **Light sampling / MIS**。共享 HLSL 实现发光三角形面积/亮度采样、HDR 亮度/立体角采样、遮挡测试和 power heuristic，与 diffuse/GGX BSDF 采样合并；方向光独立处理。切换重新累计，关闭退回 BSDF 采样。无需新依赖，仍为单向路径追踪。

验证使用仅顶灯照明的 Cornell Box，方向光、环境照明、降噪全部关闭，固定相机和随机序列；比较色调映射前的线性亮度，以 1024 spp MIS 为参考。64×64 Metal 与 Chrome WebGPU 的 8 spp MSE 从 `0.00138407` 降到 `0.0000639693`（约 95%），32 spp 从 `0.000338694` 降到 `0.0000141903`（约 96%）；128 spp 继续降到 `0.00000298023`。1024 spp BSDF/MIS 平均亮度差 0.46%。极亮 HDR 小光源及 90° 环境旋转的低采样误差和高采样能量对比也通过。低采样速度收益取决于场景；每次反弹可能多一条阴影射线，原有路径辐射上限 64 仍可能引入偏差。

复现：`ctest --test-dir build -R light-sampling -V`，网页先 `npm run docs:dev` 再 `node scripts/webgpu/check-light-sampling.mjs`。测试同时验证统一开关和累计重置；原始图像与统计输出位于 `build/validation/cornell-*-{bsdf-8,mis-8,mis-32,reference}.png`、`build/validation/mis-web.json`。

后端回归（2026-11-14）：Mac 39 项中 36 项通过、3 项因 MoltenVK 无光追而跳过；Ubuntu Vulkan Lavapipe 7 项全部通过；Windows DX12 WARP 20 项中 19 项通过、硬件光追探测跳过。软件后端的 32×32 Cornell 8 spp MSE 从约 `0.0009896` 降到 `0.00002791`，1024 spp 平均亮度差约 1.55%。Chrome 的原有求交、HDR、降噪、12 组模型/环境和隐藏恢复回归通过；构建后的静态网站、MIS/降噪开关与手机面板通过。另验证了安装包独立调用 Cornell MIS、开关重置和无 UI 构建。日志位于 `build/validation/mis-*`；Windows 真机 GPU 尚未验证。

### 双向路径追踪（BDPT）

C++ 设置 `PathTracingSettings::integrator = PathIntegrator::Bidirectional`，网页设置 `integrator: 'bidirectional'`；默认 `PathTracing` / `'path'`。两个预览面板的 Integrator 提供对应选项，原生模型例子可用 `--mode path --integrator bidirectional`。切换重新累计，兼容现有降噪。继续只维护共享 HLSL，没有新增项目依赖或 GPU 绑定。

实现从发光三角形按面积/亮度分布选取起点，双面余弦发射并沿 diffuse/GGX BSDF 生成光路；与相机路径各前缀连接，检查遮挡，并用同一面积度量下的路径策略概率计算 power heuristic。固定反弹上限内不做 roulette；统一材质/背面法线求值，光路使用伴随法线修正。MIS 通过相邻连接策略的密度比和流式 log-sum-exp 求权重，复用线程私有路径存储，避免反复复制整条路径。当前为相机条件化 BDPT：包含相机命中灯、直接连灯和多个光路顶点连接，不包含向其他像素的相机 splat。HDR 与方向光保留已有 NEE/MIS，有限发光面由 BDPT 独立处理以避免重复计光。原有静态不透明材质、粗糙度下限和路径辐射值上限 64 的限制仍适用。

Cornell（仅顶灯、无环境/方向光、无降噪，48×48，反弹上限 5）：相对 2048 spp PT 参考，8 spp MSE 从 `6.75345e-5` 降为 `1.61813e-5`（约 76%）；32 spp 从 `1.43306e-5` 降为 `4.68205e-6`（约 67%），BDPT 128 spp 为 `1.37182e-6`。2048 spp 的平均亮度差约 0.19%，Metal 和 WebGPU 的统计一致。深度 1、3、8 和带非退化 UV 的法线贴图能量审计通过。每个 BDPT sample 的成本更高；本场景 PT 128 spp MSE `3.10447e-6`，低于耗时相近的 BDPT 32 spp，因此这些数据不证明等耗时一定更优。

复现：`ctest --test-dir build -R bidirectional -V`；网页先启动 `npm run docs:dev`，再运行 `node scripts/webgpu/check-bidirectional.mjs`。记录位于 `build/validation/bdpt-*`，其中 `bdpt-web.json` 包含误差、能量、不同深度审计与 CPU 提交/GPU 完成读回的实际耗时；PNG 为真实 WebGPU 输出。

低分辨率软件后端的测试使用四组互不重叠的随机样本块比较平均 MSE，不要求每一张 8 spp 图都优于 PT。48×48 的四组均值为 PT8 `5.92625e-5` / BDPT8 `1.78599e-5`、PT32 `1.30841e-5` / BDPT32 `4.63983e-6`。混合面光源、环境与方向光的 WebGPU 能量差约 0.11%。

Windows 软件验证使用 [微软 WARP 1.0.22](https://www.nuget.org/packages/Microsoft.Direct3D.WARP/1.0.22)，仅将微软签名的 ARM64 `d3d10warp.dll` 放在隔离测试程序旁。系统自带的 `10.0.26100.9818` 可运行原 PT，但执行本 BDPT 内核会崩溃；更换此测试驱动后同一内核通过。未替换系统 DLL，也未将 WARP 加入项目依赖；实际显卡仍使用其 DX12 驱动。官方更新记录包括复杂控制流与 RayQuery 处理的修复。

最终回归（2026-11-14）：Mac 在 Metal API 校验下 41 项中 37 项通过，4 项因 MoltenVK 无光追而跳过；Ubuntu Vulkan Lavapipe 8 项全部通过，并复核最终降噪入口；Windows WARP 1.0.22 的 21 项中 20 项通过、硬件光追探测跳过。Chrome 的 BDPT 实际像素、多种反弹深度、法线与混合光照能量审计、原 PT/MIS/降噪和 12 组资源回归通过；构建后的静态文档、手机控件、安装包独立 BDPT 调用和无 UI 构建通过。Windows 真机 GPU 尚未验证。四组软件后端的 32 spp 平均 MSE 从约 `1.94909e-5` 降到 `3.90193e-6`；日志和对比图位于 `build/validation/bdpt-*`。

### 光追层级与光子映射

上层统一为 `Raster` / `RayTracing`，光追内部通过 `RayIntegrator::PathTracing`、`Bidirectional`、`PhotonMapping` 选择积分器；网页对应 `'raster'` / `'ray-tracing'` 和 `'path'` / `'bidirectional'` / `'photon-mapping'`。新增 `RayTracingSettings`、`SetRayTracing/GetRayTracing`（网页小写），旧的 PathTracing 设置接口、枚举、能力字段与网页模式输入仍可用；网页模式读取统一返回 `'ray-tracing'`。没有新增项目依赖。

光子映射继续共享 `pathtrace.hlsl`。每轮先清理 32³ 网格桶，再从面光源生成光子路径，以无丢弃的原子链表存储间接光子，最后用 Epanechnikov 表面核聚集；直接照明独立估计以避免重复计光。每轮重发射、按轮数的负六分之一次方缩小半径并平均像素估计。金属/光滑相机路径继续追踪，HDR 与方向光沿用原有估计器。仅支持当前静态不透明材质，有限半径有平滑偏差；它与显示降噪独立。

`photonsPerIteration` 默认 8192、范围 256–65536；`photonRadius` 默认 0.16、范围 0.03–0.5，单位为归一化模型世界坐标。两端面板均提供对应控件，参数变化重新累计。光子记录与网格复用原 scratch 绑定，仍为 8 个存储绑定；占用随预算和最大反弹次数增长，超出 WebGPU 限额时提示降低预算或渲染比例。

模型列表增加 Cornell Indirect（遮挡间接光）和 Cornell Glossy（小顶灯、粗糙金属球）。球使用几何法线作为着色法线，使测试聚焦于采样与密度估计；玻璃折射焦散未包含在本阶段。设置方法、实际渲染和收敛曲线见 `docs/guide/integrator-comparison.md`。基准同时报告相同相机 spp 和等时间下的整体/地面线性亮度误差，并估计高采样参考噪声，不将光子映射的平滑直接当作更准确。

复现：`ctest --test-dir build -R photon-mapping -V`；网页先启动文档，再运行 `node scripts/webgpu/check-photon.mjs` 和 `node scripts/webgpu/benchmark-integrators.mjs`。绘图工具单独安装 matplotlib 后可运行 `scripts/plot-integrator-benchmark.py`，不属于项目运行依赖。验证记录位于 `build/validation/photon-*`、`integrator-benchmark.json`；原生 Metal 与网页基础光子图逐像素 RMS 差约 `5.7e-7`。

本阶段回归：Mac 43 项中 38 项通过、5 项因 MoltenVK 缺少光追能力跳过；Ubuntu Vulkan Lavapipe 9 项全部通过；Windows WARP 1.0.22 的 22 项中 21 项通过、硬件光追探测跳过。Chrome 光子映射像素、三例子面板、原 PT 模式生命周期回归通过；安装后的独立 C++ 调用与关闭 UI 的库构建通过。Windows 真机显卡尚未验证。

### 玻璃焦散与积分器对比

新增 `nova_comparison` / 网页第六例子：`IntegratorComparison`（网页 `'integrator-comparison'`）并排显示 PT、BDPT、光子映射，同相机、仅面光源、无显示降噪；固定 spp 或每种方法的活跃时间预算用完后暂停。`comparisonSamples` 默认 64，`comparisonMilliseconds` 默认 0（非零使用时间预算）；通过 `GetComparisonProgress()` / `getComparisonProgress()` 读取各栏采样、毫秒和完成状态。预热不计入预算，每轮每栏 1 个相机样本；最后一轮可能超过时间预算。光子总路径数另计，三栏串行执行。

`assets/CornellCaustics` 为原创小顶灯、悬空玻璃球和地面焦散场景。C++ 与网页加载同一 GLB，共享 HLSL 粗糙介质 Fresnel、Snell 折射、全反射、BSDF/PDF 与重要性传输。支持 transmission factor 与 IOR；不同材质共用核心 PBR 因子和纹理图集。当前仅空气/玻璃界面，不含嵌套介质、吸收、色散或透射纹理；光栅保留不透明回退。无新增项目依赖。

```sh
./build/nova_comparison assets/CornellCaustics/CornellCaustics.glb --api metal --compare-samples 64
./build/nova_comparison assets/CornellCaustics/CornellCaustics.glb --api metal --compare-ms 1000
ctest --test-dir build -R 'glass|comparison' -V
node scripts/webgpu/check-glass.mjs
node scripts/webgpu/check-comparison.mjs
```

Metal / WebGPU 的玻璃场景 4096 spp 室内平均亮度，PT / BDPT / 光子映射约为 0.011264 / 0.011163 / 0.011125。WebGPU 独立 BSDF 探针检验入射、出射辐亮度能量与全反射；实际三栏像素、预算暂停、非整批 spp、相机重置和模式恢复均有验证。截图和日志位于 `build/validation/comparison-*`、`glass-*`。

最终回归：Mac 45 项中 40 项通过、5 项因 MoltenVK 缺少光追跳过；Ubuntu Vulkan Lavapipe 的对比、玻璃、模式、光子映射 4 项通过；Windows DX12 WARP 1.0.22 同样 4 项通过。Windows 首轮 4096 spp 玻璃 BDPT 超过 900 秒，软件后端正确性回归改用 512 spp 后通过；Metal/WebGPU 保留 4096 spp 能量和像素检查。原生实际读回像素确认横向／纵向三栏均非空、暂停图像保持不变；Chrome 静态文档、代码页签与手机上下布局通过。Windows 真机 GPU 尚未验证。
