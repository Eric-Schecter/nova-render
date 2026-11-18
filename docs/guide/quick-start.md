# Quick start

## Build

Install CMake 3.24+, a C++17 compiler and vcpkg. macOS needs Xcode/Metal Toolchain and DXC from Vulkan SDK; Windows needs MSVC Build Tools and Windows SDK; Linux needs window-system development packages and Vulkan. Drivers provide the Vulkan runtime; macOS Vulkan uses MoltenVK, while native Metal does not require it.

```sh
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset debug
cmake --build --preset debug
```

On Windows x64 use `windows-x64` in an x64 MSVC developer shell. Windows ARM64 uses `windows-msvc` in an ARM64 developer shell. macOS/Windows may set `NOVA_ENABLE_VULKAN=OFF`; Linux requires Vulkan.

## Run the triangle

```sh
# macOS
./build/nova_triangle.app/Contents/MacOS/nova_triangle --api metal
# Linux (debug preset)
./build/nova_triangle --api vulkan
```

```powershell
# Windows x64 (windows-x64 preset)
.\build-windows-x64\nova_triangle.exe --api dx12
```

Flags: `--api auto|metal|dx12|vulkan`, `--frames 120`, `--resize-test`, `--shader-dir PATH`. Windows `--warp` tests CPU D3D12; devices require Shader Model 6.0/DXIL.

Shared HLSL compiles to backend binaries embedded by default; no external shader directory is needed. See [package integration](./package) or [write an example](./integration).

## iOS Metal triangle

On an Apple Silicon Mac, use Xcode with the Metal Toolchain component and an installed iOS simulator runtime. The pilot target uses SDL3 for the window and app events and embeds the same HLSL triangle shaders as the desktop sample.

```sh
cmake -S . -B build/ios-simulator \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=arm64-ios-simulator -DVCPKG_HOST_TRIPLET=arm64-osx \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$(xcrun --sdk iphonesimulator --show-sdk-version)" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/ios-simulator --target nova_ios_triangle
xcrun simctl install booted build/ios-simulator/examples/ios/nova_ios_triangle.app
xcrun simctl launch --console-pty booted org.nova.render.ios-triangle
```

Boot one iPhone simulator before running the last two commands. Rotate it using the Simulator controls, then return to the Home Screen and reopen the app. The triangle should remain visible after each transition. The target defaults to Metal raster rendering with the parameter panel disabled.

## Browser examples

With Node.js 22+ and DXC available, run `npm ci` then `npm run docs:dev`. `npm run docs:build` creates a static site with precompiled WGSL. Published [online examples](https://eric-schecter.github.io/nova-render/) need a WebGPU browser over HTTPS; HLSL editing is local-only.
