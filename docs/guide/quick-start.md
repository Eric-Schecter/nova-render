# 快速开始

## 构建条件

需要 CMake 3.24+、C++17 编译器、vcpkg 和平台构建工具。manifest 固定依赖基线；GLFW 管理窗口，volk 用于可选 Vulkan 后端。shader 在构建时从 HLSL 生成，参见 [shader 构建](./shaders)。

macOS 使用 Xcode 与 Metal Toolchain；Windows 使用 MSVC Build Tools 和 Windows SDK；Linux 需要 GLFW 所需的 X11/Wayland 开发包，以及 Vulkan Loader 和 ICD。

## 配置与编译

在仓库根目录执行：

```sh
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset debug
cmake --build --preset debug
```

Windows PowerShell / MSVC 开发环境：

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
cmake --preset windows-msvc
cmake --build --preset windows-msvc
```

当前 Windows preset 默认 ARM64。x64 机器需使用对应 MSVC 环境并覆盖 `-DVCPKG_TARGET_TRIPLET=x64-windows`；不要把 ARM64 配置直接当作 x64 编译环境。

macOS / Windows 可在配置时设置 `-DNOVA_ENABLE_VULKAN=OFF` 仅构建原生后端，Linux 不允许关闭 Vulkan。原生构建仍需要 shader 工具。

## 运行

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

DX12 设备须支持 Shader Model 6.0 / DXIL。用 `--api dx12 --warp` 可显式验证 CPU 软件渲染路径。

程序使用构建时配置的 shader 绝对目录，当前未提供安装或分发打包；移动可执行文件后可用 `--shader-dir PATH` 指定资源。

下一步：[接入 Renderer](./integration)。
