# Ray-query feasibility probes

Optional, isolated compute tests. They build a one-triangle BLAS and TLAS, execute four rays using the same HLSL, and read back hit/miss, distance, primitive and instance indices, barycentrics, front/back facing and near clipping. No window or renderer initialization is required.

```sh
cmake -S . -B build -DNOVA_BUILD_RAY_PROBES=ON
cmake --build build
ctest --test-dir build -R '^ray-query-' -V
```

- DX12: DXC `cs_6_5` to DXIL, DXR tier 1.1 and Shader Model 6.5 required.
- Vulkan: DXC to Vulkan 1.2 SPIR-V; acceleration structures, ray query and buffer device address required.
- Metal: the same SPIR-V through SPIRV-Cross MSL 3.0, then Apple's Metal compiler. This is a real translated `intersection_query`, not a separately maintained Metal shader.

The probes return 77 when a required device capability is absent (CTest reports **Skipped**). Compilation, API, validation or readback failures are failures, not skips. All three query/shader paths remain experimental; the production renderer is not changed by enabling this option.

## Verified 2026-11-13

- Apple M4 / Metal: actual GPU AS build, dispatch and readback passed, Metal API validation enabled.
- Mac MoltenVK: ray-query/acceleration-structure extensions absent; correctly skipped.
- Windows 11 ARM64 / WARP: DXR tier 1.1, SM6.5, actual AS build, dispatch and readback passed. This is CPU software validation, not RTX hardware validation.
- Parallels Display Adapter: no D3D12 feature-level 12.0 device, correctly skipped.
- Ubuntu 24.04 ARM64 / Mesa 25.2.8 Lavapipe, LLVM 20.1.2: advertises ray-query support but default 128-bit software vector mode crashes during BLAS build. Vulkan validation reported no API errors. Single-thread/cache-disabled and indexed geometry attempts did not fix it. Using `LP_NATIVE_VECTOR_WIDTH=256` and `MESA_SHADER_CACHE_DISABLE=true` for the validation process passed the complete test. The cache must be disabled when testing different software vector widths.

For that older ARM64 **software validation driver only**:

```sh
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json LP_NATIVE_VECTOR_WIDTH=256 MESA_SHADER_CACHE_DISABLE=true \
  ctest --test-dir build -R '^ray-query-' -V
```

This matches an [upstream ARM64 Lavapipe report](https://github.com/gfx-rs/wgpu/issues/8637), including the radix-sort subgroup-width mismatch. The report identifies a replacement in newer Mesa; no Mesa upgrade or system-wide environment change was made here. The vector-width override is documented by [Mesa](https://docs.mesa3d.org/drivers/llvmpipe.html).

Physical Windows RTX and hardware Vulkan execution remain unverified in this stage. The minimum common RayQuery shader chain is viable; a full path tracer still needs per-backend integration and separate image/convergence tests. Existing raster regression checks: 26/26 passed on Mac.
