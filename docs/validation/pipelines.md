# Raster pipeline laboratory validation

Validated on the current Mac with Metal, Vulkan through MoltenVK, and Chrome WebGPU. Windows runtime validation remains deferred; all 10 native DXIL stages (eight vertex/fragment stages and two Compute Shaders) compile successfully. Narrow-browser checks validate responsive layout, not mobile GPU support.

The native `nova_pipelines_test` and `scripts/webgpu/check-pipelines.mjs` render the actual GPU passes at 321 × 181 to exercise partial tile boundaries. They cover all four pipelines, zero/128/256 lights, all seven diagnostic views, visible lighting changes, reduced candidate lists and a deliberately overflowing list that must preserve the all-light result. Additional camera/target and viewport changes cover 16 × 9, 127 × 65 and 641 × 359. Compute output also matches the preserved fragment implementation byte-for-byte for the standard four-pipeline fixture.

For 128 lights, Forward+ and Clustered Forward match Forward exactly after RGBA8 conversion. Deferred mean absolute difference is about 0.0133 out of 255 across RGBA channels, consistent with the half-precision G-buffer. At 256 lights with radius 32, overflow fallback matches Forward exactly; Deferred difference is about 0.0052. Metal, Vulkan and WebGPU RGB image comparisons have mean absolute difference below 0.15; the default Forward image matches exactly.

Both native window examples render successfully with zero reported window validation errors. Existing AA, shadow and SSR checks pass on Metal and Vulkan (six checks), alongside the two new native checks. The documentation builds successfully. The page acceptance test exercises all pipeline/debug selections, light controls, orbit/zoom, preview hide/show, narrow layout and Reset, and records screenshots with no browser page errors.

Reproduce from a configured build:

```sh
cmake --build build/lights-040 --target nova_pipelines nova_pipelines_test
ctest --test-dir build/lights-040 -R '^pipelines-(metal|vulkan)$' --output-on-failure
node scripts/webgpu/generate-pipelines.mjs
# With npm run docs:dev running at the root URL on port 5173:
NOVA_TEST_URL=http://127.0.0.1:5173 node scripts/webgpu/check-pipelines.mjs
# With the built docs preview running on port 4187:
node scripts/webgpu/check-pipelines-page.mjs
```

Raw native PPM images, WebGPU results and page screenshots are generated beneath `build/validation/pipelines/`. Build outputs are ignored by Git. Depth reduction and compact light lists now execute as Compute Shaders, with 64 threads per workgroup. The tests also verify that the generated culling artifacts contain compute entry points instead of fragment entry points. These checks establish image correctness; CPU-inclusive frame timings are not pure GPU benchmarks.

## Vulkan multi-draw indirect

Vulkan submits scene batches with multi-draw indirect when the device supports
`multiDrawIndirect` and `drawIndirectFirstInstance`. The indirect argument buffer is
reused while batch ranges stay the same. Outline masks retain per-instance
draws for selected IDs.

With a Vulkan and glTF-enabled test build, run the same geometry and camera path in both modes:

```sh
cmake --build build --target nova_indirect_draw_benchmark
NOVA_VULKAN_DRAW_TRACE=1 NOVA_VULKAN_FORCE_DIRECT=1 build/nova_indirect_draw_benchmark build/validation/indirect-draw/direct
NOVA_VULKAN_DRAW_TRACE=1 build/nova_indirect_draw_benchmark build/validation/indirect-draw/indirect
```

The benchmark uses in-memory geometry, 16 or 256 batches and 10,000, 50,000
or 125,000 instances. It records synchronized frame times after warm-up and
writes the last frame for pixel comparison. The two modes submit the same
instance ranges, with shadows disabled.
