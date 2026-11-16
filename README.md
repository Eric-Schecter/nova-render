# Nova Render

跨平台渲染库：C++17 桌面端支持 Metal、Vulkan、DX12，网页端使用 WebGPU，共享 HLSL shader。提供模型、光照、光追、动画及屏幕空间效果示例。

## 使用

从 [Releases](https://github.com/Eric-Schecter/nova-render/releases) 下载匹配平台的 C++ SDK，解压后通过 `CMAKE_PREFIX_PATH` 指定目录：

```cmake
find_package(Nova 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE Nova::Render)
```

网页端安装 Release 中的 npm 包，并复制预编译资源：

```sh
npm install https://github.com/Eric-Schecter/nova-render/releases/download/v0.1.0/nova-render-web-0.1.0.tgz
npx nova-web-assets --out public/nova
```

[安装与 API 调用](docs/guide/package.md) · [接口文档](docs/api/index.md) · [示例](docs/examples/triangle.md)

## 源码构建

需要 CMake 3.24+、C++17、vcpkg 和对应平台的 shader 工具链。

```sh
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset debug
cmake --build --preset debug
```

文档本地预览：`npm ci`，然后 `npm run docs:dev`。构建和平台细节见[快速开始](docs/guide/quick-start.md)。

第三方资源与授权见[资源声明](docs/credits.md)。项目暂未指定开源许可证。
