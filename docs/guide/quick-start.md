# Quick start

## Build

Install CMake 3.24+, a C++17 compiler and vcpkg. macOS needs Xcode/Metal Toolchain and DXC from Vulkan SDK; Windows needs MSVC Build Tools and Windows SDK; Linux needs window-system development packages and Vulkan. Drivers provide the Vulkan runtime; macOS Vulkan uses MoltenVK, while native Metal does not require it.

```sh
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset debug
cmake --build --preset debug
```

On Windows use `windows-msvc` in an MSVC developer shell. It defaults to ARM64; x64 requires an x64 compiler environment and `VCPKG_TARGET_TRIPLET=x64-windows`. macOS/Windows may set `NOVA_ENABLE_VULKAN=OFF`; Linux requires Vulkan.

## Run the triangle

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

Flags: `--api auto|metal|dx12|vulkan`, `--frames 120`, `--resize-test`, `--shader-dir PATH`. Windows `--warp` tests CPU D3D12; devices require Shader Model 6.0/DXIL.

Shared HLSL compiles to backend binaries embedded by default; no external shader directory is needed. See [package integration](./package) or [write an example](./integration).

## Browser examples

With Node.js 22+ and DXC available, run `npm ci` then `npm run docs:dev`. `npm run docs:build` creates a static site with precompiled WGSL. Published [online examples](https://eric-schecter.github.io/nova-render/) need a WebGPU browser over HTTPS; HLSL editing is local-only.
