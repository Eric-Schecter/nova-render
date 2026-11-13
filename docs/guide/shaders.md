# HLSL 与离线构建

唯一维护的 shader 源码是 `render/shaders/triangle.hlsl`，入口为 `VSMain` 和 `PSMain`。构建规则在 `cmake/Shaders.cmake`。

- D3D12：DXC → Shader Model 6.0 DXIL。
- Vulkan：DXC → Vulkan 1.0 SPIR-V。
- Metal：DXC → 独立 SPIR-V → SPIRV-Cross → MSL 2.1 → Xcode metal / metallib。

Vulkan 顶点编译使用 `-fvk-invert-y`，配合正高度 viewport；Metal 使用未翻转的中间 SPIR-V。不要复用已经翻转的模块生成 Metal shader。

## 构建工具

Windows / Linux x64 的 DXC 由固定基线的 vcpkg `directx-dxc` host 包提供；macOS SPIRV-Cross 由 vcpkg host 包提供。该基线没有 macOS / Linux ARM64 的 DXC 包，需要另行提供可运行工具。

macOS 可使用 Vulkan SDK 的 DXC，离线编译 metallib 还需要 Xcode Metal Toolchain：

```sh
xcodebuild -downloadComponent MetalToolchain
```

可在配置时显式指定：

```sh
cmake -S . -B build \
  -DNOVA_DXC_EXECUTABLE=/path/to/dxc \
  -DNOVA_SPIRV_CROSS_EXECUTABLE=/path/to/spirv-cross
```

CMake 也查找 vcpkg host 目录、`VULKAN_SDK/bin`、PATH，以及 macOS 的 `~/VulkanSDK/*/macOS/bin`。

## 产物与分发

产物生成在构建目录 `shaders/` 中。应用仅加载二进制，不运行 DXC 或 SPIRV-Cross。修改 HLSL 会触发重新编译，语法错误阻止构建；生成的 MSL、AIR、metallib、DXIL 和 SPIR-V 不提交 Git。

当前 shader 与固定三角形后端配套，不是通用 shader 插件接口；修改阶段输入输出时也要检查各后端管线契约。
