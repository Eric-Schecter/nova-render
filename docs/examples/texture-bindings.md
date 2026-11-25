<script setup>
import TextureBindingsPlayground from '../.vitepress/theme/TextureBindingsPlayground.vue'
</script>

# Texture binding benchmark

Compare per-draw texture binding, Nova's atlas strategy in isolation, and shader-selected texture array layers. The three paths draw the same 1,024 quads with 16 materials. The benchmark checks their output byte-for-byte before timing.

<ClientOnly><TextureBindingsPlayground /></ClientOnly>

CPU time covers command encoding and submission. GPU time uses pass timestamps when the adapter exposes them. Each result is the median of 30 runs, following nine warm-up runs per strategy. Texture creation and uploads happen before timing.

Texture array layers share a single texture allocation. The browser test measures switching costs and indexed texture access with portable WebGPU. Native bindless descriptors have separate resource allocation and residency rules. [WebGPU bindless draft](https://github.com/gpuweb/gpuweb/blob/main/proposals/bindless.md)

The native Metal companion measures separately allocated textures through an argument buffer. Run this from the repository root on macOS:

```sh
xcrun swiftc -O scripts/metal/benchmark-texture-bindings.swift -o /tmp/nova-texture-benchmark
/tmp/nova-texture-benchmark
```

On an Apple M4, one run measured 0.093 ms CPU and 0.189 ms GPU per draw batch with per-draw binding, 0.052 ms CPU and 0.131 ms GPU with an atlas, and 0.052 ms CPU and 0.135 ms GPU with an argument buffer. All three produced identical pixels. Small differences at this scale are sensitive to clock precision and GPU scheduling. [VKGuide GPU-driven rendering overview](https://vkguide.dev/docs/gpudriven/gpu_driven_engines/)

## Indirect drawing

Use **Compare indirect drawing** to keep the same texture atlas and draw only 768 out of 1,024 objects. The direct path builds and uploads a visible list on the CPU, then issues one instanced draw. The indirect path compacts visible IDs and writes the instance count in a compute pass, then issues one indirect draw. The benchmark verifies pixel parity. The GPU timestamp spans compute through render for the indirect path. CPU time includes visibility evaluation and upload submission on the direct path. Small timings can fall below the browser clock resolution.

This workload uses a deterministic visibility predicate and non-overlapping objects. A scene-wide GPU-driven renderer would also need bounds, camera tests and per-mesh draw batches.

## Vulkan on macOS

On Apple M4 with MoltenVK 1.4.0, `vulkaninfo` reports sampled-image nonuniform indexing, runtime descriptor arrays, partially bound descriptors, and sampled-image update-after-bind. It reports 256 sampled images per stage for regular descriptors, and 1,000,000 for update-after-bind descriptors. These are reported limits. A Vulkan pipeline still needs to enable the features it uses and validate its descriptor layout and allocation on the selected device.

The `material-vulkan` and `scene-render-vulkan` regressions establish the atlas baseline on this device, including runtime material and texture updates.

```sh
vulkaninfo | grep -E 'deviceName|driverInfo|shaderSampledImageArrayNonUniformIndexing|runtimeDescriptorArray|descriptorBindingPartiallyBound|descriptorBindingSampledImageUpdateAfterBind|maxPerStageDescriptor.*SampledImages'
ctest --test-dir build/sdl-only -R '^(material-vulkan|scene-render-vulkan)$' --output-on-failure
```

Vulkan's native model raster pipeline can select independent material textures via descriptor indexing. On a compatible Vulkan 1.2 device, forward and G-buffer shading use the indexed path when compatible materials fit into 128 distinct texture descriptors after deduplication. Material slots still obey the 512-slot model limit. Metal and DX12 retain their fixed 16-slot indexed layout. Alpha blending, transmission, and materials with extra textures packed into shared channels use the atlas. Hybrid and ray tracing use their existing texture paths. The indexed path preserves independent texture sizes and mip chains. Nova also prepares the atlas so features and passes that require it can still run. Opaque Bindless raster edits replace only changed texture descriptors and keep the model buffers, pipelines, and swapchain. The atlas is refreshed before switching to an atlas-dependent render mode. Masked or packed materials use the full update so visibility and material passes read consistent maps.

Run the A/B with the same Release build and the same material fixture:

```sh
NOVA_VULKAN_BINDING_TRACE=1 build/sdl-only/nova_material_test vulkan assets build/validation/vulkan-bindless/bindless
NOVA_VULKAN_BINDING_TRACE=1 NOVA_VULKAN_FORCE_ATLAS=1 build/sdl-only/nova_material_test vulkan assets build/validation/vulkan-bindless/atlas
```

The test captures both sides before and after replacement. Frame time includes draw, readback, presentation and wait. The replacement measurement starts at `SetMaterial` and ends once the new frame has rendered.

The same 640 × 480 camera and FXAA settings produced both captures. Across three paired runs, the medians were:

- Atlas — frame 8.29 ms, `SetMaterial` 1.74 ms, first rendered frame after replacement 169.34 ms.
- Bindless — frame 8.33 ms, `SetMaterial` 1.89 ms, first rendered frame after replacement 160.53 ms.

There was no clear steady-state frame-time gain. The replacement frame arrived about 9 ms earlier with Bindless in this fixture, while the CPU material call took slightly longer. Image PSNR between strategies was 41.20 dB before replacement and 41.13 dB after replacement. Texture addressing and mip selection differ, and both paths rendered the replacement in the intended material slot.

### Vulkan texture replacement

On the same M4 and fixture, three interleaved runs measured the following medians from `SetMaterial` through the next rendered frame:

- Atlas — 170.32 ms.
- Bindless with full model rebuild — 161.53 ms.
- Bindless with per-texture updates — 8.62 ms.

The optimized runs uploaded one texture per edit and kept the original swapchain. Readbacks from both Bindless versions were pixel-identical before and after the replacement. The timings include presentation and readback. The CPU still prepares the fallback material data during `SetMaterial`.

### Metal model textures

On macOS devices with Tier 2 argument buffers, forward and G-buffer raster shading can index independently uploaded material textures. Compatible opaque edits update only the changed entries. The atlas path covers models outside the indexed material limits, and is refreshed before an atlas-dependent mode uses it. The public material API is shared across both paths.

The same 640 × 480 FXAA fixture on Apple M4 produced these medians over three interleaved runs:

- Atlas — frame 8.38 ms, replacement through rendered frame 9.75 ms.
- Argument buffer with full rebuild — frame 8.36 ms, replacement through rendered frame 11.53 ms.
- Argument buffer with incremental updates — frame 8.42 ms, replacement through rendered frame 8.22 ms.

Full-rebuild and incremental Argument Buffer captures were pixel-identical. The incremental trace recorded one texture update per edit. To reproduce the comparison, run the material benchmark with the `metal` API and a separate output directory for each mode. Set `NOVA_METAL_FORCE_ATLAS=1` or `NOVA_METAL_REBUILD_MATERIALS=1` for the controls and `NOVA_METAL_BINDING_TRACE=1` to log texture upload counts.

### DX12 model textures

DX12 uses a shader-visible SRV table on adapters with Resource Binding Tier 2 or higher. The same material eligibility and atlas fallback rules apply. Supported edits upload only changed maps and replace their table entries after the upload fence.

On an RTX 4060 Laptop GPU, three interleaved runs of the same 320 × 240 FXAA fixture measured these medians from `SetMaterial` to the rendered replacement:

- Atlas — 60.61 ms.
- Indexed textures with full rebuild — 84.19 ms.
- Indexed textures with incremental updates — 21.03 ms.

The indexed versions matched pixel-for-pixel. Static frames were 6.76, 6.43, and 6.66 ms respectively. Set `NOVA_DX12_FORCE_ATLAS=1` or `NOVA_DX12_REBUILD_MATERIALS=1` for the controls and `NOVA_DX12_BINDING_TRACE=1` to log texture upload counts.


### Vulkan shared texture table

Vulkan stores eight texture indices per material in a storage buffer. Identical image bytes, dimensions, and color-space semantics share a descriptor. Updating one material preserves the other references. The atlas takes over if the deduplicated table exceeds the 128-entry capacity.

```sh
NOVA_VULKAN_BINDING_TRACE=1 build/sdl-only/nova_material_test vulkan assets build/validation/vulkan-shared-textures --shared-textures
NOVA_VULKAN_BINDING_TRACE=1 build/sdl-only/nova_material_test vulkan assets build/validation/vulkan-texture-overflow --texture-table-overflow
```
