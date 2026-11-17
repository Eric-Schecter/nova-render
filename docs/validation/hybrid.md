# Hybrid implementation validation

## GPU presentation foundation

Native AA/deferred devices expose their owned output texture to the model presenter. Metal, Vulkan and DX12 adapters can sample that texture directly; no automatic image readback/upload is required on this path. An explicit private capture operation remains available for verification. Queue completion is still synchronized; this stage does not claim asynchronous frames in flight or shared hardware ray-query acceleration structures.

`nova_gpu_presentation_test` verifies Forward and Deferred on Apple M4 Metal and Vulkan/MoltenVK: three consecutive GPU frames perform zero readbacks, then an explicit capture matches the legacy image path (MAE < 0.05/255). The deferred and alpha material regression tests also pass (six targeted native tests in total). DX12 runtime validation remains deferred.

## Hybrid per-light shadows

Current source exposes Hybrid as the third mode, with shared HLSL, paired settings/capabilities, static-model and instance BVHs, per-light directional/point/spot visibility, MASK coverage, OIT overlays and GPU presentation. The visibility kernel uses GPU fragment-shader BVH traversal; it does not claim hardware ray queries. AO, reflections and temporal reconstruction are validated in the following stages; DDGI remains unavailable.

`nova_hybrid_test` and `scripts/webgpu/check-hybrid.mjs` cover single-model traversal, five independent light slots, visibility diagnostics, AA restrictions, zero automatic native readbacks and Raster/Hybrid lighting parity. Metal, Vulkan and WebGPU agree within 0.015/255 maximum mean absolute channel error for these captures.

The G-buffer now stores split logarithmic view depth rather than half-float world XYZ. Two half values retain enough depth precision for ray-origin reconstruction across near/far ranges without exceeding the baseline 32-byte attachment budget. The Web visibility kernel requires DXC optimization/inlining: unoptimized function-local matrix storage loses layout through SPIR-V/WGSL lowering. Native/Web transformed-instance ray tests guard this boundary.

## Original reference geometry and camera baseline

Run `python3 scripts/reference/prepare_hybrid.py --profile reference-import`, then `node scripts/reference/hybrid-scenes.mjs`. The pinned archive and converted models remain under ignored `build/reference/hybrid-rendering`; no reference assets are bundled in the SDK or documentation example.

`nova_hybrid_reference_test metal` / `vulkan` and `scripts/webgpu/check-hybrid-reference.mjs` use the original Shadows, Reflections and Global Illumination scenes, all twelve pinned cameras, source-space transforms and the expected assembled triangle counts (147130 / 234124 / 15864). The profile fixes a shared hard directional light, no HDR, 320×180, 60° FOV and 1/1000 clipping.

The 72 captures across three backends establish framing and direct-light/shadow parity. Native Raster/Hybrid MAE is zero for the twelve views; native/Web maximum MAE is below 0.11/255. Captures were visually inspected for the pillar rows/bunny, reflection spheres and indoor Suzanne scene. This does not establish reflection/GI fidelity: those effects are still unimplemented, Pica/Sponza conversion and their eight cameras remain pending, and the original Windows executable has not been compared on this Mac.

## Regression and example UI

The 103-test native suite passed on Metal and Vulkan/MoltenVK (102 initial passes; the comparison test was updated to use an undefined enum value after Hybrid became mode 2, rebuilt and passed on rerun). WebGPU alpha-material/OIT and multi-instance light-shadow regressions passed. The six affected model/Hybrid entries also compile to DXIL; this is compilation coverage only, not Windows execution.

The native Hybrid example rendered three frames on Metal and Vulkan with zero validation errors. The built VitePress example was exercised in Chrome WebGPU: Raster/Hybrid panel rebuilding, shadow master, visibility view and AA restrictions passed. Desktop and narrow-screen screenshots are retained under `build/validation/hybrid-shadow`; the local preview serves the current build.

## Hybrid ambient occlusion

Shared HLSL traces cosine-weighted, finite-distance hemisphere rays from the reconstructed primary surface. AO multiplies ambient lighting only; direct-light visibility remains independent. Both APIs and parameter panels expose the master, radius, strength, sample count and visibility diagnostic. Metal, Vulkan and WebGPU pass contact-AO on/off and diagnostic captures together with the earlier multi-light shadow checks; native/Web maximum MAE remains below 0.015/255.

## Hybrid reflections

Nearest-hit BVH traversal evaluates one-bounce GGX specular rays, opaque/MASK coverage, material textures, direct light and secondary shadows. Misses sample the environment; BLEND remains the OIT overlay. Both APIs expose enable, maximum distance, strength, samples and a reflection-only diagnostic. A low-roughness metallic floor reflects the raised box; Metal/Vulkan/WebGPU captures and native/Web parity pass below 0.015/255. Secondary hits currently use geometric normals, with no secondary normal mapping or recursive reflection.

## Hybrid temporal reconstruction

The opaque color history is reconstructed before post processing and final AA, using camera motion, expected previous view depth, split-depth history, oct-normal rejection, instance identity and a 3×3 neighborhood clamp. History weight decreases with screen-space motion. AO/reflection sample rotations vary by pixel and frame; soft-shadow disks rotate by frame. OIT is composited after opaque reconstruction and never fed back into history.

The paired Hybrid tests cover sixteen frames, orbit motion, full-camera translation, camera cuts, FOV changes, explicit resets, SSAA and BLEND overlays. Against the mean of the same sixteen unfiltered samples, the native fixture MSE decreases from 6.587 to 0.482 (about 93%); WebGPU gives 6.584 to 0.482. These are fixture-specific 8-bit capture errors, not a general convergence or physical-accuracy guarantee. Animated geometry remains rejected, and scene/material/light edits invalidate history instead of providing moving-instance vectors. The implementation reconstructs combined color; per-effect moments/variance filtering and DDGI are separate future work.

The Web deferred kernel uses an explicit SPIR-V optimization sequence: storage matrices are inlined and scalarized, while loop transformations that introduce non-storable pointer spills are excluded. Both Naga validation and transformed-instance native/Web capture parity guard this compiler boundary.

Final regression: all 103 native CTest cases passed on Metal/Vulkan. Six targeted Hybrid/deferred/alpha tests were repeated after the inactive-diagnostic mode-switch fix. Chrome WebGPU passed the Hybrid convergence/motion/reset, SSAA and temporal-OIT combinations, plus the existing deferred, alpha-material and multi-light shadow suites. The Hybrid capture set remains within 0.015/255 native/Web MAE. Both native example backends rendered sixteen frames with zero validation errors; built documentation controls and desktop/narrow-screen previews were exercised in Chrome. Windows runtime validation remains deferred.
