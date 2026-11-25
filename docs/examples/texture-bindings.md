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

Nova currently uses the atlas for Vulkan model rendering. The `material-vulkan` and `scene-render-vulkan` regressions passed on this device, including runtime material and texture updates. These results establish the atlas baseline. Vulkan Bindless image parity and frame or update timing have yet to be measured.

```sh
vulkaninfo | grep -E 'deviceName|driverInfo|shaderSampledImageArrayNonUniformIndexing|runtimeDescriptorArray|descriptorBindingPartiallyBound|descriptorBindingSampledImageUpdateAfterBind|maxPerStageDescriptor.*SampledImages'
ctest --test-dir build/sdl-only -R '^(material-vulkan|scene-render-vulkan)$' --output-on-failure
```
