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

On an Apple Silicon Mac, use Xcode with the Metal Toolchain component and an installed iOS simulator runtime. The iOS vcpkg overlay pins SDL 3.4.20 for the UIKit scene lifecycle required by iOS 27. The triangle embeds the same HLSL shaders as the desktop sample.

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

## Android Vulkan triangle

Use an Android SDK with platform 35, Build Tools 35.0.0, Platform Tools and NDK r27c. Set `JAVA_HOME` to a JDK, `ANDROID_HOME` to the SDK directory and `SDL3_SOURCE_DIR` to the SDL3 source matching the version resolved by vcpkg. The APK uses that release's SDLActivity Java sources and the same C++ triangle entry point as iOS.

```sh
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/27.2.12479018"
cmake -S . -B build/android \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DVCPKG_TARGET_TRIPLET=arm64-android \
  -DCMAKE_SYSTEM_NAME=Android -DCMAKE_SYSTEM_VERSION=28 \
  -DANDROID_PLATFORM=android-28 -DANDROID_ABI=arm64-v8a -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/android --target nova_android_triangle
examples/android/package.sh build/android
adb install -r build/android/nova-triangle-debug.apk
adb shell am start -n org.nova.render.triangle/.NovaActivity
```

Boot an ARM64 emulator with Vulkan enabled, or connect an ARM64 Android device. The packaging script signs a local debug APK using the SDK command-line tools.

## Mobile Damaged Helmet

Both sample apps bundle the same GLB. Drag with one finger to orbit and pinch with two fingers to zoom.

```sh
cmake --build build/ios-simulator --target nova_ios_helmet
xcrun simctl install booted build/ios-simulator/examples/ios/nova_ios_helmet.app
xcrun simctl launch booted org.nova.render.ios-helmet

cmake --build build/android --target nova_android_helmet
examples/android/package.sh build/android helmet
adb install -r build/android/nova-helmet-debug.apk
adb shell am start -n org.nova.render.helmet/org.nova.render.triangle.NovaActivity
```

## Browser examples

With Node.js 22+ and DXC available, run `npm ci` then `npm run docs:dev`. `npm run docs:build` creates a static site with precompiled WGSL. Published [online examples](https://eric-schecter.github.io/nova-render/) need a WebGPU browser over HTTPS; HLSL editing is local-only.
