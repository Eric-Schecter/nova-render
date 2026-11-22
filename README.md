# Nova Render

A cross-platform C++17 and WebGPU rendering library with shared HLSL shaders.

[Documentation & live examples](https://eric-schecter.github.io/nova-render/)

## Platforms and APIs

Checked items are implemented; unchecked items are not yet supported.

- [x] macOS — Metal, Vulkan via MoltenVK
- [x] Windows — DirectX 12, Vulkan
- [x] Linux — Vulkan
- [x] Web — WebGPU, without WASM
- [ ] iOS / Android
- [ ] WebGL backend

Release **0.10.30** provides C++ SDKs for macOS ARM64, iOS devices, iOS Simulator and Android ARM64, plus a WebGPU npm package. The [Windows x64 SDK 0.10.1](https://github.com/Eric-Schecter/nova-render/releases/tag/v0.10.1) was validated with DirectX 12 and Vulkan.

## Features

- [x] Standalone Forward, Deferred, Forward+ and Clustered Forward laboratory with surface diagnostics
- [x] Model Forward / Deferred rendering, shared material G-buffer and diagnostics
- [x] Rasterization and ray-tracing modes
- [x] Shared HLSL → DXIL / SPIR-V / Metal / WGSL
- [x] glTF / GLB model loading and metallic-roughness PBR
- [x] Multiple material slots with runtime PBR/texture edits, alpha cutout, weighted OIT transparency and double-sided rendering
- [x] Perspective/orthographic cameras, full orientation and viewport rectangles
- [x] Scene ray picking with instance filters and geometric hit normals
- [x] Static scene instances with shared assets and original model space
- [x] GPU skeletal/node animation with independent scene-instance playback and posed picking/bounds
- [x] Directional lighting, skyboxes and HDR image-based lighting
- [x] Directional, point and spot light collections in raster and ray tracing; static instance scenes
- [x] Vulkan compute BVH fallback when hardware ray queries are unavailable
- [x] Scene BVH shadows for directional, point and spot lights (static instances; alpha-tested cutouts)
- [x] Shadow map laboratory — hard shadows, PCF, PCSS
- [x] Ambient occlusion — SSAO, HBAO, MSAO, RTAO
- [x] Anti-aliasing — FXAA, SMAA 1x, MSAA 4x, SSAA 2×2, TAA
- [x] HDR post-processing — bloom, auto exposure, tone mapping and color adjustments
- [x] SSR with a depth pyramid and environment fallback
- [x] Planar reflections
- [x] Local reflection probes, box projection and smooth multi-probe blending
- [x] Hybrid rendering — GPU BVH shadows, AO, one-bounce reflections and temporal reconstruction (0.9.0)
- [x] Path tracing, bidirectional path tracing, progressive photon mapping and PSSMLT
- [x] Light importance sampling, MIS and denoising
- [x] Orbit controls, parameter panels and performance indicators
- [x] C++ SDK and npm package distribution
- [ ] GPU-driven rendering, mesh shaders and VRS
- [ ] DLSS / FSR and frame generation
- [ ] DDGI / SurfelGI / VXGI
- [ ] Depth of field and motion blur

Some effects currently use dedicated example scenes rather than arbitrary models. Native ray queries require compatible GPU/API features; WebGPU uses compute BVH traversal. See the [API documentation](docs/api/index.md) for scope and limitations.

## Native: use and build

Download the [0.8.0 SDK](https://github.com/Eric-Schecter/nova-render/releases/tag/v0.8.0), verify it against the release `SHA256SUMS`, extract it, and set `CMAKE_PREFIX_PATH` to its directory. Link it from your CMake application:

```cmake
find_package(Nova CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE Nova::Render)
```

To build from source, install CMake 3.24+, a C++17 compiler, vcpkg and the platform shader toolchain:

```sh
# macOS / Linux
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset debug
cmake --build --preset debug
```

```powershell
# Windows x64: run from an x64 MSVC developer shell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
cmake --preset windows-x64
cmake --build --preset windows-x64
```

Windows ARM64 uses the `windows-msvc` preset. See [Windows GPU validation](docs/validation/windows.md) for the RTX hardware matrix and SDK checks.

See [build requirements](docs/guide/quick-start.md) and [SDK integration](docs/guide/package.md). SDK users do not need vcpkg or shader compilers; system graphics drivers and services are still required.

## Web: use and build

Install the precompiled package and copy its runtime assets into your application's public directory:

```sh
npm install https://github.com/Eric-Schecter/nova-render/releases/download/v0.8.0/nova-render-web-0.8.0.tgz
npx nova-web-assets --out public/nova
```

```js
import { createRenderer } from '@nova/render-web'
const renderer = await createRenderer({ canvas, shaderDirectory: '/nova/' })
const shader = await fetch('/nova/triangle.json').then((r) => r.json())
await renderer.setShader(shader)
await renderer.drawFrame()
```

Package users need a WebGPU-capable browser and HTTPS or localhost, but no shader compilation service. To build the web examples or package from source, use Node.js 22+ and the DXC-based shader toolchain. Documentation builds also use Python 3 to cache and verify the 206 MB reference archive; browsers load only the selected scene:

```sh
npm ci
npm run docs:dev    # Local examples and documentation
npm run docs:build # Static documentation site
npm run pack:web   # npm archive in build/packages/
```

[Package integration](docs/guide/package.md) · [Examples](docs/examples/triangle.md) · [Third-party credits](docs/credits.md)

No open-source license has been assigned to the project. Third-party components retain their own licenses.
