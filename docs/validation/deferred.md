# Deferred model validation

Validated with actual GPU rendering on Mac Metal, Vulkan through MoltenVK, and Chrome WebGPU. Windows runtime validation remains deferred; DXIL shader compilation is checked separately.

Each backend compares Forward and Deferred under Off, FXAA, SMAA, SSAA and TAA for five fixtures: unlit Damaged Helmet, PBR Damaged Helmet, independent scene instances, a paused GPU-skinned Fox pose, and a scene combining directional/point/spot lights, shadows, an HDR environment, fog and a runtime material change. This gives 25 pipeline comparisons per backend. Mean absolute error is below 0.011 out of 255 across RGBA channels. Fifty corresponding Metal/WebGPU and Vulkan/WebGPU comparisons have maximum mean error below 0.07/255.

The checks also exercise all nine G-buffer views, MSAA rejection, orthographic projection, a viewport rectangle, and clearing an empty scene after a populated one. The Web test captures actual presented frames through the browser compositor: directly copying a WebGPU Canvas after awaiting GPU completion can race swapchain expiration. No comparison accepts blank frames as a successful screenshot.

Twenty relevant native regression checks cover AA, shadows, SSR, reflection-probe blending, model materials, scene lights, scene shadows, instances, the four-pipeline laboratory and Deferred on Metal/Vulkan. The documentation page additionally checks pipeline/debug selection, incompatible MSAA options, model switching, orbit interaction and preview hide/show. Screenshots and JSON metrics are generated under `build/validation/deferred/`; build artifacts are ignored by Git.

```sh
cmake --build build/lights-040 --target nova_deferred nova_deferred_test
ctest --test-dir build/lights-040 -R '^deferred-(metal|vulkan)$' --output-on-failure
# With a root-base VitePress development server running:
NOVA_TEST_URL=http://127.0.0.1:5173 node scripts/webgpu/check-deferred.mjs
```

Native presentation retains its existing CPU image bridge. This validates material G-buffer and lighting integration, not a completed Hybrid mode or native GPU-only frame loop. Deferred MSAA is explicitly unsupported, and Forward+/Clustered Forward remain standalone laboratory pipelines.
