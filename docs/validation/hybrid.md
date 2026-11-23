# Hybrid implementation validation

## Pica Pica reflection sampling and WebGPU material variants

At 766 × 431, Chrome WebGPU reproduced pure-green pixels with DDGI and reflections enabled. A fixed camera contained 117,635 pure-green RGB pixels out of 330,146 before the material shader fix. Selecting lighting and probe shaders without the unused sheen lobe reduced that count to zero. Full- and half-resolution reflection captures also had zero pure-green pixels with and without DDGI. Scenes with sheen materials continue to select the full shader.

Temporal reflection rays now vary the sample elevation within each GGX stratum as well as the azimuth. The midpoint is preserved when temporal sampling is disabled. The moving camera comparison used one reflection ray per pixel and the same corrected WebGPU material shaders for both runs. Its synchronized median was 575.7 ms before and 580.3 ms after. The console is still brighter and more purple than the reference capture. Frame comparisons and timings are under `build/validation/hybrid-reflection-elevation`.

The twelve reference cameras rendered all 108 effect captures on each of Metal and Vulkan. Chrome's broader Hybrid shadow regression failed with both the previous and updated shader sets. The reference parity script also exceeded its Web/native threshold on the initial raster shadow fixture before exercising the updated reflection path.

## Pica Pica directional penumbra

The reference directional light samples a disk of radius 0.1 in tangent space. Nova's previous collection limit of 4 mapped to 0.024. Directional lights now accept up to 20, allowing a reference match with `shadowSoftness = 0.1 / 0.006`. Both comparison captures use ACES at exposure 1 (0 post EV), four shadow samples, shadow history, and the same fitted camera, sky, light intensity, and Hybrid settings. At 766 × 431 after 32 frames, the wider disk softens the ground shadows around the console, boxes and loose props. The reference still has a bluer ground and brighter console trim. Changing the tone curve alone does not reproduce those differences. Captures are under `build/validation/pica-reflection-current`.

## Pica Pica transport isolation

The fixed WebGPU camera uses 766 × 431, ACES at 0 EV, the same sky and directional intensity, four shadow samples at disk radius 0.1, and 32 frames of history. The image-level comparison in `build/validation/pica-transport` isolates DDGI, local probe lighting at reflection hits, and replacement of environment specular by traced reflections. The sampled floor changed from RGB (57, 98, 113) to (56, 97, 112) without DDGI. With both DDGI and reflections disabled, direct-only lighting produced (48, 82, 92) and the environment-only diagnostic (9, 27, 40). The floor difference therefore persists in direct-dominated lighting. The reference screenshot remains bluer in that area.

The console trim sample averaged RGB (41, 43, 44) before the fix and (53, 56, 57) after. Turning off hit-probe lighting or DDGI changes this sample by less than one level per channel. The correction uses height-correlated Smith GGX for reflection-ray throughput, replacing the remapped Schlick direct-light approximation. DDGI visibility now queries slightly off the geometric surface while keeping interpolation and facing at the original point. The reference console trim remains brighter than the updated renderer.

The transport profile in `nova_hybrid_reference_test` accepts a fixture with a `transport` section containing the environment path, intensity, and shadow softness. Metal and Vulkan rendered identical 766 × 432 Pica captures. Both native Hybrid tests, the Chrome WebGPU Hybrid regression and the DDGI probe kernel test passed.

## Pica Pica reference material color

The source GLTF floor material, `Plastic_Blue`, has no base color image and declares a linear RGB factor of approximately (0.1651, 0.3419, 0.4020). The pinned Assimp importer forwards that factor as the diffuse color. The reference's `dwSampleFramework/extras/ray_traced_scene.cpp` applies a power of 2.2 during GPU material upload, and its ray hit and G-buffer shaders both read that constant. The converted linear values are (0.0190, 0.0943, 0.1347). Nova's ordinary glTF importer keeps the original linear factor.

Generate the compatible comparison asset with `python3 scripts/reference/prepare_hybrid.py --profile pica-reference --output build/validation/pica-floor-color`. This profile reproduces the reference mesh assembly and converts untextured RGB factors. Alpha, textures and binary geometry were verified identical to the previous Pica comparison asset. The direct-light implementations both use cosine-weighted GGX and Lambert. The reference additionally multiplies its diffuse base by (1 − F0) before applying Fresnel, a 4% reduction for this dielectric, which does not account for the chromatic difference.

The 32-frame fixed WebGPU captures retain the same camera, ACES exposure, directional light, and radius 0.1 with four shadow samples. The floor sample averaged (57, 98, 113) before and (12, 39, 54) with the material conversion alone at the previous sky intensity of 0.18. At sky intensity 1.0, with the same HDR asset and background intensity, it averages (23, 68, 98) versus (33, 74, 103) in the reference screenshot. The higher sky intensity also raises fill inside shadows, so it is a separate comparison setting. Metal and Vulkan also rendered the converted asset under both sky strengths. Captures are in `build/validation/pica-floor-color` and `build/validation/pica-transport/{metal,vulkan}`.

## Pica Pica indirect fill in shadows

With the converted materials and sky intensity 1.0, the selected floor patch under the console was zero in the direct-only render. Disabling DDGI or traced reflections increased the patch brightness, while disabling AO also raised it. The reference's AO uses a ray length of 7 world units and an upsample power of 1.2. Nova's transport comparison had used radius 1 and strength 1.

For the reference comparison, set `aoRadius: 7` and `aoStrength: 1.2` while leaving the shared default values in place for other scenes. The native transport fixture accepts these settings, and the Web uses the same values through `setHybrid`. At the same camera, exposure, sky, directional shadow sampling and four AO samples, the selected shaded patch averaged (18, 37, 55) before and (8, 19, 30) after, against reference RGB (7, 16, 24). The illuminated floor changed by less than one 8-bit code value per channel. Wider AO produces more visible noise at this sample count than the reference's separately filtered AO. The Web comparison is under `build/validation/pica-shadow-fill`.

## Pica Pica independent AO reconstruction

The AO path traces four rays per sampled pixel. The optimized case uses one sampled pixel per 2×2 block, the independent motion/depth/normal/instance history, then a 3×3 bilateral filter and guided upsampling. The spatial result is used only for lighting; reprojection reads the unfiltered AO history. Both sides used AO radius 7, strength 1.2, and the same camera, exposure, sky, lights, reflections and shadow settings.

On Apple M4, synchronized median still/moving frame times in milliseconds changed from 228.6/223.8 to 165.2/174.0 on Metal, 229.5/237.1 to 187.0/188.4 on Vulkan/MoltenVK, and 388.8/374.8 to 302.2/300.3 on Chrome WebGPU. Chrome's independent history without spatial filtering measured 295.5/300.1 ms. The runs were sequential, with 32 warm-up frames and 16 timed frames per segment. The moving camera translates 0.045 world units along X per frame.

In the Web capture, the shadow-patch mean display luma was 17.60 before and 17.25 after. The mean absolute deviation from the four-neighbor luma average in the shadow interior was 2.05 before, 1.17 with independent history alone, and 1.11 with spatial filtering. At the final camera position, the console-region image error between the moving frame and the settled frame fell from 6.15 to 5.40/255. This includes sampling noise as well as temporal error. Captures are in `build/validation/pica-ao-filter` and `build/validation/pica-transport/{metal,vulkan}`.

## Pica Pica independent reflection reconstruction

A fixed-camera and 16-frame X-translation comparison uses one ray per reflection sample and retains the previous independent AO settings (radius 7, strength 1.2, four samples per half-resolution pixel). The optimized profile uses half-resolution reflections with full-resolution fallback for pixels without compatible guides, separate reflection history, and three À-Trous passes. The spatial luma tolerance blends center and compatible tap variances. These weights are computed only after depth, material, normal and clearcoat compatibility checks.

At the same pose on Chrome WebGPU, four-neighbor display-luma deviation fell from 4.23 to 1.69 on the console panel and 1.11 to 0.40 on the shaded floor. At the last moving pose, console-region MAE against the settled capture fell from 5.40 to 4.50/255. That error includes both temporal lag and sample noise. The main gain comes from the separate history and spatial filtering. Averaging center and tap variances only modestly improves this fixture over center variance alone.

Synchronized median still/moving frame times in milliseconds changed from 177.8/174.1 to 104.3/104.2 on Metal, 170.6/179.6 to 101.1/108.2 on Vulkan/MoltenVK, and 310.1/311.9 to 227.9/233.8 on Chrome WebGPU. The comparisons include the full Hybrid frame, not only AO and reflections. The Web captures and motion-to-settled comparisons are under `build/validation/pica-reflection-filter`. The native transport fixture accepts `halfResolutionReflections`, `reflectionDenoising` and `reflectionSVGF`, and native captures are under `build/validation/pica-transport/{metal,vulkan}`.

## Pica Pica WebGPU BVH performance

With the same 16-frame X translation and AO, shadow, reflection and temporal settings, diagnostic variants separated the main costs. The initial median-split run measured 258.5 ms per moving frame. Freezing only probe updates measured 260.8 ms, within run variation; the update dispatch measured about 4 ms by timestamp. Replacing material shading for half-resolution reflection hits measured 217.0 ms, then skipping the half-resolution traversal measured 196.3 ms. Independently skipping the full-resolution fallback measured 222.9 ms. Each diagnostic affects output, so timings are differential whole-frame estimates and should not be added. Concurrent render-pass timestamp windows can overlap.

An experimental default-texture shortcut preserved images but regressed moving frame time from 256.1 to 283.5 ms, so it was reverted. Using existing binned SAH with the same shader packages changed median still/moving times from 247.6/251.7 to 45.9/53.7 ms. The production automatic selection measured 49.4/54.7 ms, and disabling DDGI on that path measured 47.8/54.1 ms. BVH construction dropped from 1,415 to 184 ms in the measured browser run, and node count fell from 32,767 to 26,905.

Web scene assets now automatically select SAH for static meshes with at least 32,768 triangle records. Both explicit strategies remain supported. AO, reflection denoising, fallback rays and lighting settings remain identical. At 766 × 431 the maximum channel difference was one 8-bit code value in both Web captures. Native Metal and Vulkan retain their existing BVH path. Timing JSON and screenshots are in `build/validation/pica-reflection-perf`.

### Full-resolution reflection fallback after SAH

The same 766 × 431 Pica Pica fixture and 16-step camera translation were tested in Chrome WebGPU on Apple M4 with SAH forced in all variants. The diagnostic held contact shadows, four-sample AO and reflection denoising fixed. Disabling only the fallback trace changed the isolated deferred composition median from 3.22 to 1.45 ms. This 1.77 ms delta is an estimate from serialized GPU passes. Normal moving-frame medians fluctuated between 52.4 and 62.7 ms with fallback and 51.9 and 53.8 ms without it across two orders. The isolated half-resolution reflection trace pass still took 6.4–8.2 ms. Serialized pass timings should be interpreted separately from whole-frame timings.

The prototype expanded the search to a 3 × 3 neighborhood around the nearest half-resolution sample only after the existing 2 × 2 upsample failed. It matched material ID, log depth, roughness, geometric and shading normals, and kept the original fallback for mirror and clearcoat receivers. A diagnostic mask at the last moving pose counted 1,799 → 1,648 fallback pixels out of 316,059 classified reflection receivers (0.57% → 0.52%). The mask disables final temporal reconstruction so pixel classes can be counted directly. The isolated composition median was 3.16 ms with the prototype. The two normal moving-frame medians were 53.4 and 53.3 ms. The whole-frame runs did not establish a repeatable speedup or reach the 33.3 ms target for 30 FPS.

Metal and Vulkan (MoltenVK) both passed the native Hybrid tests and rendered the fixed and moving cameras with the prototype. On each backend, the moving capture differed from the baseline by more than eight 8-bit codes in at least one channel at 19 of 330,912 pixels. Production retains the original 2 × 2 upsample and full-resolution trace for rejected pixels. The diagnostic captures, including the upstream fixed view, baseline, prototype and moving comparison, are under `build/validation/pica-fallback`.

### Half-resolution reflection tracing after SAH

The same Pica Pica camera path was tested on Chrome WebGPU on Apple M4. The diagnostic serialized GPU passes and rotated the order of the half-resolution reflection variants at each of the 16 camera positions. All variants used the same SAH scene and one reflection ray per sampled pixel. They affected only the half-resolution pass and held the full-resolution fallback, AO, contact shadows and reflection denoiser fixed.

The full half-resolution pass took 7.75 ms at the median. Keeping the closest hit, barycentric coordinates and hit distance but replacing its lighting with a diagnostic color saved a paired median of 2.68 ms. Suppressing the closest-hit BVH search in that diagnostic saved a further 0.67 ms. Skipping point, spot and directional shadow evaluation at the reflected hits alone saved 2.18 ms against the full pass and visibly changed the reflections. These deltas include shader execution and scheduling effects and are not additive. Hit shading, particularly its shadow visibility queries, was the larger cost on this fixture.

The shared shader now skips punctual-light work at a secondary hit when both its base shading normal and any active clearcoat normal face away from the light. Those lobes contribute zero direct radiance. The check applies to directional, point and spot lights and retains the separate area-light path. The pass median changed from 7.75 to 7.35 ms, with a paired median saving of 0.31 ms. The alternative BVH-bound test and material-texture shortcuts did not improve the paired median and were discarded.

With the full production shaders, synchronized Chrome moving-frame medians were 59.45 → 56.80 ms and 63.95 → 52.50 ms in the two test orders. Still medians were 65.45 → 62.70 ms and 52.10 → 62.10 ms. The spread in whole-frame timings limits the attributable speedup to the directly measured reflection-pass result. The fixed and last moving captures were pixel-identical before and after on WebGPU, Metal and Vulkan (MoltenVK), compared within each backend. Captures and timing data are under `build/validation/pica-half-trace`.

## Sponza reference import

The first camera from the pinned Sponza fixture renders all 262,267 triangles at 640 × 360 on Metal, Vulkan (MoltenVK) and Chrome WebGPU. The initial OBJ-to-GLB export assigned full glTF transmission to 24 of 25 materials, including the stone floor. The exported floor base-color image contains the expected stone texture. The base-color view was nearly black and changing culling or alpha coverage alone left the transparent appearance. Removing the unintended transmission extension from this comparison asset restored the floor, columns and fabric across all three backends. Nova's glTF transmission implementation was left intact.

The comparison uses the same 512-pixel texture cap before and after. The resulting asset is about 35 MiB. The original 117 MiB conversion closed the Chrome page during loading in this run. The screenshots in `build/validation/sponza-reference` use the shared validation lighting. The reference still has a more localized sun patch and darker galleries. Authored lighting, sky fill, and indirect illumination remain to be matched.

### Sponza sunlight and shadow fill

Angle 1 uses a directional light transformed from the reference's Z rotation of 30° and X rotation of −10°. Nova's direction toward the light is approximately `(-0.4924, 0.8529, -0.1736)`. The comparison uses source intensity 10, disk radius 0.08 and four shadow samples. The floor's sun patch becomes localized along the left arcade with the source direction. The environment asset used to vary fill is the existing Pica sky, so this is a transport comparison with a substitute sky.

At 640 × 360, the reference, validation baseline, direct sun alone, and sun with sky intensity 0.40 and DDGI gave mean display luma of 214.5/163.2/214.2/215.6 on the sunlit floor, 18.8/99.1/0.0/18.6 on the left wall, and 26.0/142.5/0.0/12.0 on the right column. These are 11 × 11 pixel patches on a 0–255 scale. At sky intensity 0.15 the right column measured 43.1 with DDGI disabled and 8.8 with it enabled. Increasing the sky alone does not match both sides of the arcade. The next comparison should isolate probe coverage and the local diffuse bounce. Captures are under `build/validation/sponza-reference`.

### Sponza probe coverage

At sky intensity 0.40 after 48 frames, the WebGPU comparison held the camera, direct light, AO and reflections fixed. Geometry picking located the left wall at approximately `(232, 15, 172)` and the right pillar at `(274, 40, -73)`. With GI disabled the left/right patches averaged display luma 43.3/92.9. Per-pixel bounce left these patches unchanged to one decimal place. The scene-wide DDGI grid gave 15.3/11.1. A 5 × 4 × 5 volume covering both points, from `(135, -42, -100)` to `(335, 108, 200)`, gave 8.6/27.6 versus the reference's 18.8/26.0. The sunlit floor stayed near 215.7. The right pillar is sensitive to the probe layout. The localized grid also darkens the left wall and changes the fill deeper in the hall, so it remains a comparison setting. The tested WebGPU fixed-camera frame medians were 89.7 ms for the scene-wide grid, 116.3 ms for the tighter grid and 123.1 ms for per-pixel bounce. These single-session timings include all passes and should not be used as isolated probe cost estimates.

The native transport fixture accepts optional `probeBounds`, `probeGI`, `probeSkyLighting`, `diffuseBounce`, and `frames` to reproduce these comparisons. The screenshot triptych is `build/validation/sponza-reference/probe-comparison.png`.

### Sponza penumbra and distance fill

The first camera retains the local probe volume, sun direction, sunlight intensity, exposure, AO and reflection settings. With the coherent four-sample shadow pattern the visibility view shows large steps on the floor. Shadow SVGF alone changed the final image only slightly. Enabling per-pixel blue-noise rotation and radial jitter breaks up the bands. Use it with the existing shadow history and spatial filter. Both four and eight samples showed comparable final brightness in the measured patches. In the 48-frame Chrome sessions the synchronized medians were approximately 109 and 178 ms respectively, so the comparison setting uses four. The isolated shadow view displays raw visibility and does not include temporal or spatial filtering.

Reducing substitute sky intensity from 0.40 to 0.22 at the same camera reduced the far-wall display luma from 53.8 to 32.1 (reference 14.2). The sunlit patch stayed at 215.7 (reference 214.5). The shaded floor sample dropped from 19.5 to 13.2 (reference 47.3), so the lower sky level addresses the far wall but also exposes the remaining uneven indirect fill. The comparison uses a scene-local setting. Screenshots and the raw penumbra crop are under `build/validation/sponza-reference`.

### Sponza diffuse-light decomposition

At the first camera and sky intensity 0.22, diagnostic shader variants separately present residual sky diffuse, probe diffuse and probe coverage. The far-wall patch is outside the local volume near `(-301, 43, 48)`. Its reference/baseline display luma is 14.2/32.1, and the isolated sky diffuse is 32.1. The shaded floor patch near `(208, -0.75, -44)` is fully covered. Its reference/baseline luma is 47.3/13.2, with isolated probe diffuse 13.1. Bypassing the probe visibility weight for a diagnostic left that floor value at 13.1. The displayed component values are tone-mapped and should not be added.

Extending the volume to the rear wall while retaining the old vertical placement yielded 20.0 at the far wall and 6.8 on the floor. Starting the lowest probe layer above the floor, with bounds `(-350, 1, -100)` to `(335, 151, 200)`, gives 21.4/23.5 and holds the sunlit floor at 216.2 (reference 214.5). This is a Sponza comparison setting. Blue-noise shadows, four shadow samples, AO, reflection filters and exposure were held fixed. Metal and Vulkan captures match each other exactly, and the mean absolute RGB difference from WebGPU is 0.18/255. The comparison and component sheet are under `build/validation/sponza-reference`.

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
