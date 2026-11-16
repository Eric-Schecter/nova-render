# 快速开始

## 编译

准备 CMake 3.24+、C++17 编译器和 vcpkg。macOS 需要 Xcode / Metal Toolchain 及 Vulkan SDK 的 DXC；Windows 需要 MSVC Build Tools 和 Windows SDK；Linux 需要窗口开发包及 Vulkan 环境。Vulkan 运行时由 GPU 驱动提供；macOS 的 Vulkan 后端使用 MoltenVK，原生 Metal 无需 Vulkan 运行时。

在仓库根目录执行：

```sh
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset debug
cmake --build --preset debug
```

Windows 在 MSVC 开发环境中使用 `windows-msvc` preset，当前默认 ARM64；x64 需选择 x64 工具链并设置 `VCPKG_TARGET_TRIPLET=x64-windows`。

macOS / Windows 可设置 `NOVA_ENABLE_VULKAN=OFF` 只构建原生后端。Linux 必须启用 Vulkan。

## 运行三角形

```sh
# macOS
./build/nova_triangle.app/Contents/MacOS/nova_triangle --api metal
# Linux（debug preset）
./build/nova_triangle --api vulkan
```

```powershell
# Windows（windows-msvc preset）
.\build-windows-vulkan\nova_triangle.exe --api dx12
```

常用参数：`--api auto|metal|dx12|vulkan`、`--frames 120`、`--resize-test`、`--shader-dir PATH`。Windows 可用 `--warp` 测试 CPU 渲染，真实 DX12 设备须支持 Shader Model 6.0 / DXIL。

shader 仅维护 `render/shaders/triangle.hlsl`，构建时生成对应后端二进制。默认使用库内二进制资源，无需外部 shader 文件；安装接入见[作为依赖使用](./package)。

接下来：[编写自己的示例](./integration)。
