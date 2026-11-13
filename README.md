# Nova Triangle

最小 C++17 桌面渲染示例：同一份窗口和验证流程，通过 `render` 模块选择 Vulkan、DX12 或 Metal，绘制同一个彩色三角形。

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
