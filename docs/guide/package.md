# 作为依赖使用

Nova 提供 C++17 的 `Nova::Render` target。内置三角形 shader，默认不需要配置资源路径。

## 构建并安装一次

```sh
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset debug -DNOVA_BUILD_EXAMPLES=OFF
cmake --build --preset debug
cmake --install build --prefix /absolute/path/to/nova-install
```

构建库需要原有 shader 工具链；使用安装包的项目不需要 DXC、SPIRV-Cross、Node.js 或 vcpkg。

默认 `NOVA_BUNDLE_DEPENDENCIES=ON` 会将 vcpkg 的 GLFW / volk 开发文件及版权信息一起安装，保留 Debug / Release 布局。静态 triplet 优先；动态 triplet 仍需分发 GLFW DLL / 动态库。若由使用方自行管理依赖，可关闭此选项。

## 在自己的项目中链接

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyExample LANGUAGES CXX)
find_package(Nova 0.1 CONFIG REQUIRED)
add_executable(my_example main.cpp)
target_link_libraries(my_example PRIVATE Nova::Render)
```

配置时传入 `-DCMAKE_PREFIX_PATH=/absolute/path/to/nova-install`。推荐使用 `render::Window` 管理窗口，应用无需包含或直接链接 GLFW。底层 `Renderer` 仍接受外部 `GLFWwindow*`，供需要自行管理窗口的集成使用。

可直接构建独立示例：

```sh
cmake -S examples/consumer -B build/consumer \
  -DCMAKE_PREFIX_PATH=/absolute/path/to/nova-install \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build/consumer
./build/consumer/nova_consumer
```

完整代码在 `examples/consumer/main.cpp`。`RendererConfig::shaderDirectory` 留空即可使用库内 shader；非空则严格使用指定目录，不回退内置资源。

## 分发边界

安装目录可移动，但包必须匹配目标平台、CPU 架构和编译器 ABI。Windows 还需匹配 MSVC Runtime；目前没有自动生成跨平台二进制发布包。

GPU 驱动、系统 Metal / D3D12、Vulkan Loader / ICD，以及 Linux 窗口系统等运行环境仍由目标机器提供。

本次已在 macOS ARM64 验证安装目录迁移、独立 consumer 构建，以及 Metal / Vulkan 运行；Windows / Linux 的新安装包流程尚未验证。
