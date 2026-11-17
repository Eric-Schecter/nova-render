# Scene animation validation

The 0.9.0 scene API uses one shared vertex/skin-source range per asset, with a separate palette and playback state per instance. HLSL applies the instance palette offset in forward, G-buffer, OIT and motion vertex paths. Native and Web share the shader.

`nova_scene_animation_test` and `scripts/webgpu/check-scene-animation.mjs` cover two clips and two shared-asset instances: one paused while the other seeks/plays, explicit clock advancement, paused end-frame seek, invalid state/delta rejection, transform/scene updates, material changes and removal. Pixel checks require the paused half of the image to remain identical while the other instance moves. A generated rigid-node GLB covers TRS clips without a skin. Pose-grid ray tests and current world bounds must change with the pose while the other instance stays fixed. Static instances remain in the same scene.

Metal, Vulkan/MoltenVK and Chrome WebGPU pass actual GPU pose tests and cross-backend captures; current maximum native/Web mean channel error is below 0.04/255. Pose geometry is reconstructed from the same palette only when queries request it, then cached until the palette changes; raster rendering does not upload deformed vertices each frame.

Scene animation is application-clock driven on both ends; no native window or browser RAF auto-advances instances. Morph/CUBICSPLINE and dynamic animated shadow casting are unsupported. Animated instances receive static shadows; their caster flag does not enable dynamic shadows. Hybrid/ray mode changes are rejected before replacing state. Windows runtime validation remains deferred.

The paired 0.9.0 release is built from clean source `2e3754d79be2239ec1ffecfcf2b66404d82d1c08`. Relocated C++ consumers passed Metal and Vulkan; an offline npm consumer passed actual Chrome WebGPU rendering and the new instance animation calls. No producer checkout libraries or npm network dependencies are required by those consumers.

The 105-test native regression initially passed 103 tests: the previous static-only scene assertion was updated and passed separately; the unrelated 32768-spp Vulkan caustic energy audit timed out. Its 2048-spp smoke run passed the same statistical comparison and photon-radius sweep in 55 seconds; this does not replace the full-sample audit. Existing WebGPU alpha/OIT and Hybrid regressions also passed.
