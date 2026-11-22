# Hybrid implementation validation

## Pica Pica reflection sampling and WebGPU material variants

At 766 × 431, Chrome WebGPU reproduced pure-green pixels with DDGI and reflections enabled. A fixed camera contained 117,635 pure-green RGB pixels out of 330,146 before the material shader fix. Selecting lighting and probe shaders without the unused sheen lobe reduced that count to zero. Full- and half-resolution reflection captures also had zero pure-green pixels with and without DDGI. Scenes with sheen materials continue to select the full shader.

Temporal reflection rays now vary the sample elevation within each GGX stratum as well as the azimuth. The midpoint is preserved when temporal sampling is disabled. The moving camera comparison used one reflection ray per pixel and the same corrected WebGPU material shaders for both runs. Its synchronized median was 575.7 ms before and 580.3 ms after. The console is still brighter and more purple than the reference capture. Frame comparisons and timings are under `build/validation/hybrid-reflection-elevation`.

The twelve reference cameras rendered all 108 effect captures on each of Metal and Vulkan. Chrome's broader Hybrid shadow regression failed with both the previous and updated shader sets. The reference parity script also exceeded its Web/native threshold on the initial raster shadow fixture before exercising the updated reflection path.

## GPU presentation foundation

Native AA/deferred devices expose their owned output texture to the model presenter. Metal, Vulkan and DX12 adapters can sample that texture directly; no automatic image readback/upload is required on this path. An explicit private capture operation remains available for verification. Queue completion is still synchronized; this stage does not claim asynchronous frames in flight or shared hardware ray-query acceleration structures.

`nova_gpu_presentation_test` verifies Forward and Deferred on Apple M4 Metal and Vulkan/MoltenVK: three consecutive GPU frames perform zero readbacks, then an explicit capture matches the legacy image path (MAE < 0.05/255). The deferred and alpha material regression tests also pass (six targeted native tests in total). DX12 runtime validation remains deferred.

## Hybrid per-light shadows

Current source exposes Hybrid as the third mode, with shared HLSL, paired settings/capabilities, static-model and instance BVHs, per-light directional/point/spot visibility, MASK coverage, OIT overlays and GPU presentation. The visibility kernel uses GPU fragment-shader BVH traversal; it does not claim hardware ray queries. AO, reflections and temporal reconstruction are validated in the following stages; DDGI remains unavailable.

`nova_hybrid_test` and `scripts/webgpu/check-hybrid.mjs` cover single-model traversal, five independent light slots, visibility diagnostics, AA restrictions, zero automatic native readbacks and Raster/Hybrid lighting parity. Metal, Vulkan and WebGPU agree within 0.015/255 maximum mean absolute channel error for these captures.

The G-buffer now stores split logarithmic view depth rather than half-float world XYZ. Two half values retain enough depth precision for ray-origin reconstruction across near/far ranges without exceeding the baseline 32-byte attachment budget. Instance and BVH records store matrix columns as explicit vectors. The shared shader reconstructs matrices after loading the record, so SPIR-V to WGSL lowering uses the same layout for whole records and individual fields. Previously, a whole-record load could read vertex color as transmission and skip opaque shadow casters. The five-light Hybrid fixture and ordinary scene-shadow fixture verify visible shadows in Chrome WebGPU. The tested Hybrid captures agreed with native within 0.0282/255 mean channel error.

## Original reference geometry and camera baseline

Run `python3 scripts/reference/prepare_hybrid.py --profile reference-import`, then `node scripts/reference/hybrid-scenes.mjs`. The pinned archive and converted models remain under ignored `build/reference/hybrid-rendering`; reference assets remain outside the SDK. The online example prepares separate documentation-only copies.

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

## Original reference scenes with Hybrid effects

Generate the effect profile with `node scripts/reference/hybrid-scenes.mjs --effects`, then run `nova_hybrid_reference_test metal build/reference/hybrid-rendering/scenes-effects.json`, repeat for `vulkan`, and run `node scripts/webgpu/check-hybrid-reference.mjs --effects` with the validation server running. The default commands retain the earlier shadow-only baseline.

The original Shadows Test, Reflections Test and Global Illumination Test now cover all twelve fixed cameras at 640×360, retaining their materials, source-space geometry and instance transforms. Each view captures Raster, shadow-only Hybrid, AO, AO visibility, reflections, reflection visibility, combined lighting, temporal first frame and temporal frame 16: 108 images per backend, 324 across Metal, Vulkan/MoltenVK and Chrome WebGPU. Native frame presentation performs no automatic CPU readbacks; captures and sample-average references explicitly request them. Triangle-count and GPU-validation checks pass. Native/Web maximum RGB MAE is 0.127/255 across these captures.

For angle 1 of each scene, compare the first and reconstructed images against the average of the same sixteen unfiltered sample phases. Native MSE changes are 372.49→65.42 (Shadows), 76.08→7.11 (Reflections) and 33.68→1.81 (Global Illumination), reductions of 82.4%, 90.7% and 94.6%; WebGPU agrees. Images were inspected for ground contact, sphere/floor reflections, room corners and residual sampling noise. Sixteen frames remain visibly noisy in glossy regions. These fixture-specific errors measure reconstruction consistency, not bias against a ground-truth path tracer.

This establishes effect operation and cross-backend agreement on original geometry and cameras. It does not establish pixel fidelity to the reference executable: the profile retains a shared directional light/analytic studio environment, not the original authored lighting and DDGI. Pica Pica and Sponza conversion and the original Windows executable remain outside this validation. Raw assets and captures stay in ignored local caches and are not included in SDK packages.

## Online reference scene selection

Example 19 defaults to Reflections Test and also offers Shadows Test, Global Illumination Test and the procedural box. Documentation builds verify the pinned archive and prepare only the five required GLBs in a separate `hybrid-reference` public directory (about 22 MB total). Browser requests load the selected scene only; changing camera presets does not reload models. This directory is excluded from SDK/npm packaging. Sources and notices are listed in the footer credits.

`scripts/webgpu/check-hybrid-page.mjs` exercises the built page, four scene choices, all twelve reference camera presets, effect controls, Raster/Hybrid switching, parameter collapse and narrow-screen overflow. Reference orbit/zoom also preserves 60° FOV and 1/1000 clipping; Reset view restores the original camera position.

## Materials at reflected hits

The Damaged Helmet mirror fixture changes only the reflected helmet's normal scale and material AO strength. Both changes affect the mirror region on Metal, Vulkan/MoltenVK and Chrome WebGPU. The paired Hybrid regression has a maximum native/Web mean channel error of 0.015/255. The original three reference scenes and twelve cameras were rerun with the updated shader. Across the 324 captures, the maximum native/Web RGB mean error is 0.127/255.

## Diffuse bounce

The red-wall and neutral-floor fixture compares the same pose and lighting with diffuse bounce off and on. The test checks for added red light in the floor region on Metal, Vulkan/MoltenVK and Chrome WebGPU. The combined Hybrid regression, including the new captures, stays below 0.015/255 native/Web mean channel error. The four-ray estimator provides a baseline for probe-based GI and uses the existing optional temporal reconstruction.

## GI room performance baseline

Run the existing reference fixture at 320 × 180 with the dedicated baseline mode:

```sh
build/lights-040/nova_hybrid_reference_test metal build/reference/hybrid-rendering/scenes.json --bounce-baseline
build/lights-040/nova_hybrid_reference_test vulkan build/reference/hybrid-rendering/scenes.json --bounce-baseline
node scripts/webgpu/check-hybrid-reference.mjs --bounce-baseline
```

The last command uses the same local WebGPU validation server as the reference checks. For each of the four fixed cameras, each mode warms up four frames before taking the median of 15 synchronized frames. The timed interval includes frame submission, rendering and the wait for GPU completion. Presentation and scheduling can affect it. Image capture, model loading and setup are outside the interval. Compare modes on the same backend and machine.

On Apple M4 at 320 × 180, the four per-camera medians ranged from 18.3–35.3 ms with bounce off and 32.1–40.1 ms with pixel bounce on Metal. Vulkan/MoltenVK measured 29.6–34.9 ms off and 40.3–47.4 ms on. Chrome WebGPU measured 5.8–13.7 ms off and 18.4–23.9 ms on. Pixel bounce changed all four views. The largest native/Web mean channel error across captures was 0.101/255. Captures and timing JSON are saved under `build/validation/hybrid-gi-baseline`.

These results establish the existing per-pixel baseline for DDGI. Probe-cache updates and sampling are outside this comparison.

## Probe update kernel

`HybridProbeUpdateCS` traces one batch of a world-space probe grid through the shared material-aware BVH. Each probe stores six directional lobes with RGB diffuse radiance, validity and normalized first and second hit-distance moments. Previous and destination buffers are separate. The caller must initialize invalid history to zero and preserve untouched ranges when advancing batches.

`node scripts/webgpu/check-hybrid-probe-update.mjs` compiles HLSL to WGSL and dispatches a partial batch against an empty scene. It checks untouched probes, fresh miss distances and irradiance/moment blending against initialized history. The same entry compiles to Metal and validates as Vulkan SPIR-V. This isolated test does not exercise backend frame scheduling, nonempty-scene radiance, deferred sampling or leak suppression.

The shared sampler blends up to eight neighboring probes. It selects directional lobes by the shading normal for radiance and by the probe-to-surface direction for distance moments. A squared variance bound reduces contributions behind blockers; blocked probes keep their normalization weight so a lone blocked probe cannot recover its original brightness through renormalization. The WebGPU kernel test verifies a visible sample, a blocked sample, an out-of-grid sample and directional weighting. This is a synthetic visibility check, not an image-level leak test in the reference room.

### DDGI frame-loop comparison

```sh
build/lights-040/nova_hybrid_reference_test metal build/reference/hybrid-rendering/scenes.json --probe-comparison
build/lights-040/nova_hybrid_reference_test vulkan build/reference/hybrid-rendering/scenes.json --probe-comparison
node scripts/webgpu/check-hybrid-reference.mjs --probe-comparison
```

Metal, Vulkan and WebGPU dispatch the same HLSL update entry. They preserve untouched probes, update up to eight per frame and sample the cache during deferred lighting. The four-camera comparison warms up for 52 probe frames (four complete grid sweeps). The test preserves the off and per-pixel modes and compares all three image sets. Native metrics include synchronized presented frame times and render-only times, excluding swapchain presentation from the latter. Captures and timing JSON are in `build/validation/hybrid-gi-baseline`.

On Apple M4 at 320 × 180, the four per-camera medians for per-pixel versus probes were 8.34–9.75 versus 4.89–8.21 ms on Metal and 9.52–10.64 versus 10.01–11.00 ms on Vulkan/MoltenVK, measured without native presentation. In Chrome WebGPU the synchronized frame medians were 10.30–14.10 versus 5.20–6.60 ms. Probe timings include the scheduled update every frame. The maximum native/Web mean RGB error among probe captures was 0.077/255. This comparison measures agreement across backends, not accuracy against ground truth.

## CarConcept showroom acceptance

Use the Carmine CarConcept showroom from nova-editor's `output/car-showroom-hybrid/showroom.zip`. The reference archive SHA-256 is `403db653403b44610e0c40af646916dc9c7ffc416b0e3cde0e374d82f84a3f42`. The original CarConcept attribution and the adaptation notes are in the showroom archive. Keep these application assets separate from the SDK and the existing reference scenes.

Start the nova-editor Web preview and run from the renderer checkout. Set `NOVA_EDITOR_URL` if the editor uses a different port.

```sh
git show 676880a:render/shaders/model.hlsl > /tmp/nova-hybrid-shadow-baseline.hlsl
node scripts/webgpu/check-hybrid-showroom.mjs \
  ../nova-editor/output/car-showroom-hybrid/showroom.zip \
  /tmp/nova-hybrid-shadow-baseline.hlsl
```

The A/B run loads the same five model instances and the project's main camera. It uses the same viewport, lights, AO, reflections, one shadow sample, one AO sample, and temporal reconstruction. It records median frame time after four warm-up frames for a fixed camera and a repeatable lateral camera move. Both captures must stay within 0.15/255 mean absolute channel error. Timing is recorded for per-device comparison. Captures and JSON are written to `build/validation/hybrid-showroom`.

Hybrid skips BVH shadow traversal outside a local light's range or spotlight cone. Directional shadows and the selected-light raw visibility diagnostic retain their existing behavior.

### WebGPU pass profiling

The same showroom can be profiled without editing the installed SDK. Start the editor Web preview on port 5237 or set `NOVA_EDITOR_URL`. Use a Chrome WebGPU adapter that exposes `timestamp-query`.

```sh
node scripts/webgpu/profile-hybrid-showroom.mjs \
  ../nova-editor/output/car-showroom-hybrid/showroom.zip
```

The profiler uses one frame scheduler, discards four warm-up frames, and records sixteen normal frames and eight serialized diagnostic frames per case. It runs fixed and moving cameras with full effects, AO off, reflections off, and both off. Each case keeps the shadow budget, resolution, main camera, and temporal mode fixed.

`build/validation/hybrid-showroom/gpu-profile.json` reports normal frame timings separately from median GPU pass timings for G-buffer, shadows, combined lighting, temporal reconstruction, display, and other render passes. The diagnostic submits each pass separately and waits for completion to avoid overlapping timestamp windows on Metal-backed Chrome. Serialized timings describe isolated passes and should not be summed to predict a normal frame. Compare the lighting time across cases to estimate AO and reflection costs. These estimates include interactions between the effects. Submission-to-completion is a browser-side interval that includes GPU execution and completion notification. It is not a direct measure of queue residency, and the total frame time also includes work outside the instrumented passes.

Apple M4 baseline for the 1269 × 845 editor canvas with all effects enabled. Normal frame medians were 88.0 ms still and 88.7 ms moving. With reflections disabled they were 22.0 ms still and 21.4 ms moving. In the serialized diagnostic, the combined lighting pass took 74.9/74.0 ms and shadows took 6.3/6.5 ms. These results identify reflections as the largest effect cost on this fixture. They do not isolate traversal from reflection-hit shading.

#### Reflection traversal and instance bounds

Use `--reflection-split` on the profiler for the full reflection and reflection-disabled cases, then repeat with `--reflection-split --cheap-hit`. The cheap-hit diagnostic retains the closest hit, distance, and barycentrics, and substitutes a simple hit color. It measures the remaining traversal and ray setup together. Its output is for timing only.

At 1269 × 845 on Apple M4 WebGPU, the isolated lighting pass measured 68.9/68.3 ms for still/moving views with full reflection, 59.8/62.7 ms with cheap hit shading, and approximately 8–11 ms with reflection disabled. These are separate diagnostic runs.

The production path now tests conservative world-space bounds before transforming a ray into the instance BVH. Static instance bounds come from the existing local BVH root and are refreshed when transforms change. A/B verification uses the same viewport and per-effect ray budgets.

```sh
git show 243416e:render/shaders/model.hlsl > /tmp/nova-hybrid-bounds-baseline.hlsl
node scripts/webgpu/check-hybrid-showroom.mjs \
  ../nova-editor/output/car-showroom-hybrid/showroom.zip \
  /tmp/nova-hybrid-bounds-baseline.hlsl --instance-bounds
```

The WebGPU fixture measured 81.9 → 75.0 ms still and 86.0 → 73.6 ms moving, with zero pixel difference in both captures.

The 1800 × 1120 native Metal moving capture was pixel-identical in both test orders. The Metal renderer did not gain speed on this fixture. Fixed-camera medians were 73.4 → 75.0 ms and 72.1 → 79.7 ms, while moving medians were 89.1 → 92.1 ms and 94.1 → 94.3 ms. Metal and Vulkan passed the Hybrid and scene-render regressions.
