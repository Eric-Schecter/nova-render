# Package integration

Project/repository: **Nova Render** / `nova-render`; CMake project: `NovaRender`. Compatible consumer names remain `find_package(Nova)`, `Nova::Render` and `@nova/render-web`.

Native uses a C++17 SDK and web uses an npm archive. Both use the CMake project version. Packaging commands generate local files without publishing. Downloads are in [GitHub Releases](https://github.com/Eric-Schecter/nova-render/releases).

## C++ SDK

Configure a Release build in your vcpkg environment:

```sh
cmake -S . -B build/sdk -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DNOVA_BUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF
npm run pack:sdk -- --build-dir build/sdk --config Release
```

Without Node.js, run `cmake --build build/sdk --target nova_render` and `cpack --config build/sdk/CPackConfig.cmake -C Release`. The npm wrapper writes `.tar.gz`, `.zip` and SHA-256 files into `build/packages/`; direct CPack uses `build/sdk/packages/`.

Names follow `nova-render-sdk-version-system-architecture-compiler-configuration`. `share/Nova/sdk.json` records compiler/runtime/build settings. Bundled headers, static libraries, shaders and notices include GLFW/volk/optional ImGui. SDK consumers need no Nova checkout, vcpkg, DXC, SPIRV-Cross or Node.js. Match OS, architecture, ABI and Windows runtime; Release SDKs do not contain Debug Nova binaries.

In another project:

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyApp LANGUAGES CXX)
find_package(Nova 0.2 CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE Nova::Render)
```

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/absolute/path/to/extracted-sdk
cmake --build build --config Release
```

When switching SDK directories, use `cmake --fresh` to clear cached `Nova_DIR`. Do not use Nova's vcpkg toolchain for SDK consumption. `render::Window` encapsulates GLFW; lower-level external-window integration remains available. Dynamic-triplet DLLs/libraries must ship with the app; the Windows consumer copies SDK DLLs.

`cmake --install build/sdk --prefix /path/to/sdk` remains supported. Disable `NOVA_BUNDLE_DEPENDENCIES` only if consumers manage dependencies themselves.

## Web npm package

Generate from the Nova repository:

```sh
npm run pack:web
```

This produces `build/packages/nova-render-web-0.2.0.tgz` and its SHA-256 file. It contains renderer modules, precompiled WGSL, SMAA tables and notices, excluding the documentation site, models, HDR maps and compiler tools.

In another project:

```sh
npm install /path/to/nova-render-web-0.2.0.tgz
npx nova-web-assets --out public/nova
```

```js
import { createRenderer } from '@nova/render-web'
const renderer = await createRenderer({ canvas, shaderDirectory: '/nova/' })
const shader = await fetch('/nova/triangle.json').then((r) => r.json())
await renderer.setShader(shader)
await renderer.drawFrame()
```

Deploy `public/nova/` with your app; pass the correct asset URL, ending in `/`, for subpath hosting. There are no runtime npm dependencies or WASM. Users need no DXC/SPIRV-Cross/Nova dev server. HLSL-only `setShader()` still needs a compilation service; packaged `vert`/`frag` shaders do not.

Install the generated `.tgz`, not the documentation package at the repository root. The package is marked private/UNLICENSED to prevent accidental registry publishing; this does not prevent installing its release archive or grant an open-source license.

## Independent validation

```sh
node scripts/packaging/check-sdk.mjs /path/to/sdk.tar.gz metal Release
node scripts/packaging/check-sdk.mjs /path/to/sdk.zip vulkan Release
node scripts/packaging/check-web.mjs build/packages/nova-render-web-0.2.0.tgz
```

Scripts copy consumers outside the repository. Native validation relocates the SDK, checks all imported library locations, then exercises two probes and a window. Web validation installs offline, copies assets and runs triangle/probe APIs in a real WebGPU browser.

Projects: `examples/sdk-consumer` and `examples/npm-consumer`. Validated SDKs: macOS ARM64 Metal/Vulkan, Windows ARM64 DX12 WARP; npm: Chrome WebGPU. No Linux SDK has been generated/validated. Drivers, OS graphics/window services and browser WebGPU remain runtime requirements. Models and environment maps are application-owned.

The 0.2 SDK candidate includes formal scene, camera, picking, material, GPU shadow and atmosphere APIs. Native/Web packages carry their source commit. Use `scripts/packaging/pair.mjs` to create a SHA-256 manifest; mixing revisions or dirty builds is rejected. This does not publish a GitHub Release.
