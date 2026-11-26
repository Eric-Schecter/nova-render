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

Enable GPU frustum culling with `NOVA_VULKAN_GPU_CULL=1`. The HLSL compute
shader tests conservative instance bounds, compacts visible records within
each batch and updates its indirect instance count. It applies to eligible
static raster scenes. Animated scenes, transparency, TAA, outlines and
debug views use the existing submission path. Point and line batches
remain visible because their geometry expands in clip space.

With a Vulkan and glTF-enabled test build, run the same in-memory geometry
and camera path in all three modes:

```sh
cmake --build build --target nova_indirect_draw_benchmark
export NOVA_VULKAN_DRAW_TRACE=1 NOVA_VULKAN_DRAW_METRICS=1
NOVA_VULKAN_FORCE_DIRECT=1 build/nova_indirect_draw_benchmark build/validation/gpu-cull/direct
build/nova_indirect_draw_benchmark build/validation/gpu-cull/indirect
NOVA_VULKAN_GPU_CULL=1 build/nova_indirect_draw_benchmark build/validation/gpu-cull/culled
```

The benchmark uses 16 or 256 batches and 10,000, 50,000 or 125,000 instances.
After warm-up it measures CPU command recording plus queue submission, Vulkan
GPU timestamps for the forward command buffer and submitted instance counts.
The CPU timer starts after swapchain acquisition and excludes instance uploads
and fence waits. GPU time includes the cull dispatch and draw commands. The
benchmark captures the final view with FXAA in a separate untimed frame.
Shadows are disabled throughout.

### Heavier meshes and visibility sweep

Append `--heavy` to the three benchmark invocations to run 4,096 instances
across 64 batches with 352 or 1,472 triangles per instance. Visibility stays
at approximately 10%, 50% or 90% along the same camera track. Each mode
submits all 4,096 instances unless GPU culling is active. Expected culled
counts are 409, 2,048 and 3,686.

For frame-time comparisons, set `NOVA_VULKAN_BENCH_IMMEDIATE=1` and check
that the trace reports `immediate`. This requests immediate presentation if
the surface supports it. The regular renderer uses FIFO. Run the modes
again in reverse order to check for drift. The Apple M4/MoltenVK timing
runs used `NOVA_VULKAN_BENCH_NO_VALIDATION=1`.

On the M4 test, the 1,472-triangle workloads with 10% or 50% visibility
reduced synchronized median frame time by 24–32% versus direct drawing
in the two runs. At 90% visibility the reduction was about 8–9%. The
352-triangle workloads showed no consistent end-to-end improvement.
CPU record-and-submit is wall time and can include driver wait. Read it
alongside GPU timestamps and synchronized frame time. The final images
matched across modes in every case.

### CPU submission and driver-wait breakdown on macOS

`NOVA_VULKAN_DRAW_METRICS=1` also reports wall time for command recording,
`vkQueueSubmit`, presentation, frame-fence wait, image acquisition and
the benchmark's final device-idle wait. On macOS, it records the calling
thread's CPU time inside recording, submission and presentation. Subtract
thread CPU time from wall time on each sample to estimate time the calling
thread spent off CPU. This estimate can include scheduling delays or work
on another thread. It does not identify an internal driver call stack.

For the 1,472-triangle, 50%-visible workload on Apple M4, the two runs with
culling recorded about 0.007–0.012 ms of commands. Queue submission took
0.78–1.20 ms of wall time and only 0.10–0.14 ms of calling-thread CPU
time. Fence and image acquisition times stayed below 0.02 ms in these
runs.

Setting `MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=0` for the benchmark process
reduced the same queue calls to about 0.002 ms. The device-idle wait rose
from about 7.5–7.6 ms to 8.7–8.8 ms. The synchronized frame median stayed
around 8.8–9.0 ms across both settings. Keep the driver default for
normal runs. Asynchronous submission is useful here as a diagnostic for
where the calling thread waits.

### Visible instance indices

With GPU culling enabled, also set `NOVA_VULKAN_CULL_INDICES=1` to try
the index-list path. The compute shader writes one 32-bit index per
visible instance. The model vertex shader uses that index to look up
the original record. The draw command's instance count and batch ranges
retain the same meaning as in record-copy mode. The extra list reserves
16 KiB for the 4,096-instance heavy benchmark, compared to 768 KiB of
compaction space in record-copy mode.

Run the same `--heavy` benchmark with and without the index flag and
reverse the order for a repeat. Keep the MoltenVK queue submission
setting at its default. Check the trace for `GPU culling (indices)` or
`GPU culling (records)`. The index path requires the additional storage
binding and its two Vulkan shader variants. The record-copy path remains
available for devices or shader directories that lack them.

On the M4/MoltenVK repeat at 1,472 triangles and 50% visibility, copy and
index modes submitted the same 2,048 instances. Queue submission was
1.18 vs 0.98 ms, GPU time was 6.12 vs 6.31 ms, and synchronized frame
time was 9.58 vs 9.34 ms. The first run gave 8.37 vs 9.68 ms for the
same workload. The memory reduction was reproducible and the end-to-end
speedup was not. Keep the index variant opt-in for further comparison.
All six stress captures and all 12 SimpleInstancing frustum-edge views
matched between modes.

### GPU cull and draw timestamps

The draw metrics trace also includes GPU time for the indirect argument
reset, cull dispatch, handoff and scene draw. The intervals come from
Vulkan timestamps in the same forward command buffer. The cull interval
includes the reset-to-compute dependency. The draw interval includes the
render pass and its vertex and fragment work. It does not separately
measure vertex shader time. The trace omits phase fields when the driver
reports markers out of order.

On the Apple M4, the 1,472-triangle, 90%-visible workload spent about
0.04 ms in culling and 10.7–11.0 ms in drawing before shader reuse. The
indices mode added 0.17–0.32 ms to the draw interval in the two runs.
The 50%-visible workload changed direction between runs, so these
measurements do not isolate the cost of the index fetch.

The shared vertex shader now loads the original instance record once and
reuses its transform, winding and skin palette data. The generated SPIR-V
shows one instance-buffer access in that vertex entry point. In the
90%-visible workload, the default record-copy path's drawing interval
fell to about 10.40 ms in both repeats. All six captures matched before
and after in both modes. Record copy remains the default cull output.

## Metal indirect commands and GPU culling

On supported Apple Silicon Macs, `NOVA_METAL_INDIRECT=1` enables cached,
CPU-encoded indirect command buffers for forward scenes. With
`NOVA_METAL_GPU_CULL=1`, the shared HLSL frustum kernel compacts visible
instance records and updates draw counts. A small Metal kernel translates
those draw ranges into ICB commands. The ICB model shaders use argument
buffers for both atlas and indexed materials. Forward scenes with AA Off
or FXAA are covered. Other passes use the existing draw path.
[Apple documentation on GPU encoding](https://developer.apple.com/documentation/metal/encoding-indirect-command-buffers-on-the-gpu)

Run the same heavy geometry and camera path used by the Vulkan benchmark:

```sh
cmake --build build --target nova_indirect_draw_benchmark
export NOVA_METAL_DRAW_TRACE=1 NOVA_METAL_DRAW_METRICS=1 NOVA_METAL_BENCH_OFFSCREEN=1
build/nova_indirect_draw_benchmark build/validation/metal-icb/direct --heavy --metal
NOVA_METAL_INDIRECT=1 build/nova_indirect_draw_benchmark build/validation/metal-icb/indirect --heavy --metal
NOVA_METAL_GPU_CULL=1 build/nova_indirect_draw_benchmark build/validation/metal-icb/culled --heavy --metal
```

The benchmark records CPU encoding and commit time separately. It uses
Metal stage-boundary timestamps for the frustum dispatch, ICB encoding
and render pass. GPU timestamps are converted using paired CPU and GPU
samples. The render interval includes vertex and fragment work. The
offscreen flag removes display synchronization from the timing run.
[Apple Metal timestamp conversion](https://developer.apple.com/documentation/metal/converting-gpu-timestamps-into-cpu-time)

On Apple M4 at 1,472 triangles per instance and 50% visibility, the
reverse-order run measured 11.47 ms direct, 11.45 ms with a CPU-encoded
ICB and 6.32 ms with a GPU-culled ICB. The GPU frustum and ICB encoding
dispatches took about 0.011 and 0.004 ms respectively. The culled path
submitted 2,048 of 4,096 instances. All six stress captures matched across
modes. The 125-instance SimpleInstancing fixture also matched at 12
frustum-edge views while using indexed materials.
