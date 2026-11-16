# Windows GPU validation

Validated on Windows 11 x64 with an NVIDIA GeForce RTX 4060 Laptop GPU (driver 560.94), MSVC 19.44, CMake 4.2 and Vulkan SDK 1.4.328.1. Tests use an isolated `nova-render-validation` workspace.

## Results

- Complete matrix: **100 passed, 2 explicitly skipped, 0 failed** across 102 tests. The skips are WARP MLT and the four-integrator comparison, which requires MLT.
- DX12 hardware: raster models, multiple assets/instances, runtime materials and textures, all raster AA methods, cameras/viewports, environment lifecycle, UI, scene shadows and ray-tracing mode transitions.
- DXR: RTX hardware selected (`software=0`), ray-tracing tier 1.1 and Shader Model 6.5. The ray-query probe checks hits, misses, primitive/instance IDs, distances, barycentrics and near clipping.
- Vulkan: RTX hardware selected, acceleration structures, ray queries and ray-tracing pipelines reported available. Nova uses inline ray queries. Validation layers were enabled.
- PT, BDPT, photon mapping, light sampling/MIS, glass, MLT convergence, AO, shadows, post-processing, SSR, planar reflections and probes were exercised by the matrix. DX12/Vulkan hardware also run dedicated glass, AO and integrator-comparison tests.
- Actual DX12 and Vulkan model windows were captured and inspected. The path tracer rendered Damaged Helmet with its HDR environment. Independent SDK ray images retain the separate red/blue material slots.
- After rebuilding from the exact clean source, the 18-case focused matrix completed with 16 passes, the same 2 expected skips and no failures.
- Mac regression: 8 Metal/Vulkan checks passed, including materials, scene rendering/shadows, path tracing and MLT. Chrome WebGPU static-site mode controls, refinement, disposal and mobile panel bounds passed.

## Fixes and limits

MSVC required explicit Vulkan image-aspect flag conversions. DX12 structured-buffer draws now pass vertex and instance offsets explicitly instead of depending on draw-system IDs. Vulkan model/material updates invalidate AA resources and pipelines that depend on the old model state. CMake sets `NOVA_D3D12=1` for the matching DXIL model shaders; rebuild external shader overrides from the same revision.

DX12 WARP can report DXR support, but MLT initialization removed its software device on this host. Current source builds expose `metropolis` in capabilities, reject unsupported MLT before GPU submission, and omit it from the native selector. Other tested WARP paths remain available. WebGPU compute BVH continues to support MLT.

The v0.2.0 release archives predate these fixes. No existing release assets were replaced. This report does not establish compatibility with all Windows GPUs, Windows ARM64 or Linux hardware.

## Reproduce

Use an interactive Windows desktop for swapchain/window tests. An SSH service session can execute the offscreen ray-query probes but cannot necessarily create a swapchain. This validation used temporary interactive scheduled tasks under the already logged-in user, without a password or global timeout/driver changes.

From an x64 MSVC developer shell:

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
cmake --preset windows-x64 -DCMAKE_BUILD_TYPE=Release -DNOVA_TEST_DX12_HARDWARE=ON -DNOVA_BUILD_RAY_PROBES=ON
cmake --build --preset windows-x64
ctest --test-dir build-windows-x64 --output-on-failure
```

Alternatively, `scripts/windows-validate.ps1` accepts `-Vulkan -Hardware -Architecture x64 -Configuration Release`. Hardware tests are opt-in; regular DX12 tests retain WARP coverage.

## Independent SDK

A UI-enabled Windows x64 Release SDK was built from clean commit `0ae95af1814b833836f6bd1a9ae3810b632aca4f`, using the 0.2.0 package version:

`nova-render-sdk-0.2.0-windows-amd64-msvc-release.zip`

SHA-256: `5060dab0b07fea77fe3329c4d0c7c4d713af2056e4a2170a18b7f94b8e29c0d8`

Both DX12 hardware and Vulkan independently extracted and relocated the SDK, built a separate CMake application, verified imported libraries stayed inside the SDK, and exercised probes, models, runtime material slots/maps, scene instances, picking, camera/viewport, atmosphere, a window and ray accumulation. MLT ran on the RTX device. No producer checkout, vcpkg toolchain or shader compiler was used by the consuming CMake project.

```powershell
node scripts/packaging/check-sdk.mjs path\to\sdk.zip dx12-hardware Release
node scripts/packaging/check-sdk.mjs path\to\sdk.zip vulkan Release
```

A matching web archive and paired SHA-256 manifest are generated from the same clean commit. The Windows SDK is a local validation artifact, not a new GitHub Release asset. Logs, result JSON, captures and package files remain under the isolated workspace; Mac copies are under `build/validation/windows/` and `build/packages/0.2.0-windows-x64-0ae95af/`.
