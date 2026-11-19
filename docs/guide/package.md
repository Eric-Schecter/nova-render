# Package integration

Project/repository: **Nova Render** / `nova-render`; CMake project: `NovaRender`. Compatible consumer names remain `find_package(Nova)`, `Nova::Render` and `@nova/render-web`.

Native uses a C++17 SDK and web uses an npm archive. Both use the CMake project version. Packaging commands generate local files without publishing. Downloads are in [GitHub Releases](https://github.com/Eric-Schecter/nova-render/releases).

## Download 0.10.4

[Release v0.10.4](https://github.com/Eric-Schecter/nova-render/releases/tag/v0.10.4) provides a macOS ARM64 Release C++ SDK, a WebGPU npm archive, and SHA-256 checksums. The latest Windows x64 SDK is [v0.10.1](https://github.com/Eric-Schecter/nova-render/releases/tag/v0.10.1). The macOS SDK and Web package are paired in `manifest.json` for nova-editor. Both desktop SDKs bundle SDL3. Applications using `render::Window` keep the same API. Migrate code constructing a renderer from an external window to `render::Window`.

Download and verify the pair on macOS:

```sh
mkdir -p nova-0.10.4
cd nova-0.10.4
release=https://github.com/Eric-Schecter/nova-render/releases/download/v0.10.4
curl -fL "$release/manifest.json" -o manifest.json
curl -fL "$release/SHA256SUMS" -o SHA256SUMS
curl -fL "$release/nova-render-sdk-0.10.4-darwin-arm64-appleclang-release.zip" -O
curl -fL "$release/nova-render-web-0.10.4.tgz" -O
shasum -a 256 -c SHA256SUMS
unzip nova-render-sdk-0.10.4-darwin-arm64-appleclang-release.zip
```

Use the extracted SDK directory containing `include/`, `lib/` and `share/` as `CMAKE_PREFIX_PATH`. The SDK includes optional ImGui integration. Sample models and environments belong to the application.

### nova-editor

From the editor checkout, download and install the matching pair directly:

```sh
pnpm sdk:install https://github.com/Eric-Schecter/nova-render/releases/download/v0.10.4/manifest.json
pnpm sdk:verify
```

Existing projects keep their locked version until explicitly updated. The updated nova-editor integration uses `pnpm sdk:update 0.10.4`; older editor versions need their SDK compatibility policy updated first. The installer validates platform, architecture, features, hashes and embedded source revisions before replacing packages. A local downloaded manifest also works.

## Build a C++ SDK

Configure a Release build in your vcpkg environment:

```sh
cmake -S . -B build/sdk -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DNOVA_BUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF
npm run pack:sdk -- --build-dir build/sdk --config Release
```

Without Node.js, run `cmake --build build/sdk --target nova_render` and `cpack --config build/sdk/CPackConfig.cmake -C Release`. The npm wrapper writes `.tar.gz`, `.zip` and SHA-256 files into `build/packages/`; direct CPack uses `build/sdk/packages/`.

Names follow `nova-render-sdk-version-system-architecture-compiler-configuration`. `share/Nova/sdk.json` records compiler/runtime/build settings. Bundled headers, static libraries, shaders and notices include SDL3, volk, and optional ImGui. SDK consumers need no Nova checkout, vcpkg, DXC, SPIRV-Cross or Node.js. Match OS, architecture, ABI and Windows runtime; Release SDKs do not contain Debug Nova binaries.

In another project:

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyApp LANGUAGES CXX)
find_package(Nova 0.8 CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE Nova::Render)
```

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/absolute/path/to/extracted-sdk
cmake --build build --config Release
```

When switching SDK directories, use `cmake --fresh` to clear cached `Nova_DIR`. Do not use Nova's vcpkg toolchain for SDK consumption. `render::Window` manages the SDL3 window and event loop. Dynamic-triplet DLLs/libraries must ship with the app; the Windows consumer copies SDK DLLs.

`cmake --install build/sdk --prefix /path/to/sdk` remains supported. Disable `NOVA_BUNDLE_DEPENDENCIES` only if consumers manage dependencies themselves.

## Web npm package

Generate from the Nova repository:

```sh
npm run pack:web
```

This produces `build/packages/nova-render-web-0.10.4.tgz` and its SHA-256 file. It contains renderer modules, precompiled WGSL, SMAA tables and notices, excluding the documentation site, models, HDR maps and compiler tools.

In another project (or use a verified local `.tgz` path):

```sh
npm install https://github.com/Eric-Schecter/nova-render/releases/download/v0.10.4/nova-render-web-0.10.4.tgz
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
node scripts/packaging/check-web.mjs build/packages/nova-render-web-0.10.4.tgz
```

Scripts copy consumers outside the repository. Native validation relocates the SDK, checks all imported library locations, then exercises two probes and a window. Web validation installs offline, copies assets and runs triangle/probe APIs in a real WebGPU browser.

Projects: `examples/sdk-consumer` and `examples/npm-consumer`. Version 0.10.4 was validated with macOS ARM64 Metal/Vulkan and Chrome WebGPU. The Windows x64 SDK was last validated with DirectX 12/Vulkan in v0.10.1. Drivers, OS graphics/window services and browser WebGPU remain runtime requirements. Models and environment maps are application-owned.
