# 作为依赖使用

桌面端使用 C++17 SDK，网页端使用独立 npm 压缩包。版本统一为 CMake 中的 Nova 版本。当前流程只生成本地文件，不创建 GitHub Release 或执行 npm publish。

## C++ SDK

先配置 Release 构建（在已有 vcpkg 环境中）：

```sh
cmake -S . -B build/sdk -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DNOVA_BUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF
npm run pack:sdk -- --build-dir build/sdk --config Release
```

也可直接使用 `cmake --build build/sdk --target nova_render` 和 `cpack --config build/sdk/CPackConfig.cmake -C Release`，无需 Node.js。产物在 `build/packages/`（直接调用 CPack 时为 `build/sdk/packages/`）：`.tar.gz`、`.zip` 和脚本生成的 SHA-256 文件。包名包含版本、系统、架构、编译器和配置；`share/Nova/sdk.json` 记录构建信息。

SDK 包含头文件、静态库、内置 shader、依赖库与声明。默认捆绑 GLFW／volk／可选 ImGui，使用方无需 Nova 源码、vcpkg、DXC、SPIRV-Cross 或 Node.js。包必须匹配目标系统、架构、编译器 ABI 和 Windows Runtime；Release SDK 不包含 Debug 版 Nova。

解压后，在另一个项目中：

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyApp LANGUAGES CXX)
find_package(Nova 0.1 CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE Nova::Render)
```

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/absolute/path/to/extracted-sdk
cmake --build build --config Release
```

使用方不要传入 Nova 的 vcpkg 工具链。推荐 `render::Window` 管理窗口，无需直接包含 GLFW；外部窗口集成仍可使用底层接口。动态 triplet 的 DLL／动态库必须随应用分发；Windows 独立验证例子会复制 SDK 自带的相应 DLL。

原有 `cmake --install build/sdk --prefix /path/to/sdk` 安装方式也保留。若自行管理第三方依赖，可关闭 `NOVA_BUNDLE_DEPENDENCIES`。

## 网页 npm 包

在 Nova 工程中生成：

```sh
npm run pack:web
```

得到 `build/packages/nova-render-web-0.1.0.tgz` 及 SHA-256 文件。包内只有渲染模块、预生成 WGSL、SMAA 表和依赖声明，不含文档站、示例模型、HDR 环境资源或 shader 编译工具。

在另一个项目中：

```sh
npm install /path/to/nova-render-web-0.1.0.tgz
npx nova-web-assets --out public/nova
```

```js
import { createRenderer } from '@nova/render-web'
const renderer = await createRenderer({ canvas, shaderDirectory: '/nova/' })
const shader = await fetch('/nova/triangle.json').then(r => r.json())
await renderer.setShader(shader)
await renderer.drawFrame()
// 页面退出：renderer.dispose()
```

应用需正常部署 `public/nova/`；子路径部署时传入对应资源 URL，以 `/` 结尾。包没有运行时 npm 依赖，无 WASM；使用者不需要 DXC、SPIRV-Cross 或 Nova 开发服务器。仅传 HLSL 源码给 `setShader()` 仍需要额外编译服务，使用包内的预编译 shader 则不需要。

当前安装对象是生成的 `.tgz`，不是仓库根目录的文档工程。包标记为 `private`／`UNLICENSED`，避免意外公开发布；没有更改项目授权。

## 独立验证

```sh
node scripts/packaging/check-sdk.mjs /path/to/sdk.tar.gz metal Release
node scripts/packaging/check-sdk.mjs /path/to/sdk.zip vulkan Release
node scripts/packaging/check-web.mjs build/packages/nova-render-web-0.1.0.tgz
```

验证脚本将消费项目复制到仓库外临时目录。C++ 检查依赖全部来自迁移后的 SDK，再调用双探针与窗口 API；网页离线安装包、复制资源，并在真实 WebGPU 浏览器中调用三角形与双探针 API。

独立项目分别见 `examples/sdk-consumer` 与 `examples/npm-consumer`。已验证 macOS ARM64 的 Metal／Vulkan SDK、Windows ARM64 的 DX12 WARP SDK，以及 Chrome WebGPU npm 包；尚未生成和验证 Linux SDK。

GPU 驱动、操作系统图形／窗口服务、浏览器 WebGPU 支持仍由运行机器提供。示例模型和环境贴图由应用自行提供。
