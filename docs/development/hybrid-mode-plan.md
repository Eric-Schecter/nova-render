# Hybrid mode: reference audit and implementation plan

Status: GPU texture presentation and the Hybrid per-light shadow stage are implemented in current source and validated on Metal, MoltenVK and WebGPU. Published 0.8.0 predates Hybrid. AO, ray-traced reflections, DDGI and temporal reconstruction remain subsequent stages; complete reference fidelity is not yet claimed.

## Reference strategy

The audited reference is [diharaw/hybrid-rendering 1.1.0](https://github.com/diharaw/hybrid-rendering/releases/tag/1.1.0), commit `090360e0b231e6df38f0c0ce0b7465c8f8485b0e`.

Its frame sequence in `src/main.cpp` is:

1. Rasterize a G-buffer: base color/metallic, packed normals/motion, roughness/curvature/mesh ID/linear depth, and hardware depth.
2. Trace soft shadows and AO; reproject history and run spatial filters.
3. Trace/update DDGI irradiance and distance probes, with visibility-aware interpolation and recursive indirect lighting.
4. Trace glossy reflections, including DDGI lighting at secondary hits; filter and upsample.
5. Deferred PBR composition, TAA, exposure/tone mapping and UI.

The separate ground-truth path tracer is a comparison view. It does not replace the raster primary visibility in the hybrid path. Source: `g_buffer.*`, `ray_traced_{shadows,ao,reflections}.*`, `ddgi.*`, `deferred_shading.*`, `temporal_aa.*`.

## Proposed public boundary

Add `RenderMode::Hybrid` / `setRenderMode('hybrid')` as the third mode, retaining the numeric values and compatibility alias of the existing two modes. Add paired `HybridSettings` / `setHybrid` and capability reporting for the implemented effects, scene restrictions and hardware/compute ray execution. Exact settings fields remain a design proposal, not an available API.

Models, instances, camera/viewport, material slots, light collections, environments and performance reporting stay shared across all modes. Hybrid configuration selects shadows, AO, reflections, DDGI, ray budgets and denoising. PT/BDPT/photon/MLT remain ray integrators. API users do not manage G-buffer images or backend descriptors.

First acceptance targets static reference geometry, camera/light motion and material edits. Animated ray geometry requires separate acceleration-structure updates; existing raster GPU skinning does not establish that support.

## Scope: a substantial pipeline extension

Six coupled work packages are required. This is larger than adding a mode enum or enabling an existing SSR option.

1. **Shared GPU frame resources.** Native raster/AA and path devices currently expose CPU image readbacks and independently owned resources. Hybrid needs shared devices/queues, GPU surface views, acceleration structures, barriers and resource lifetimes. Keep CPU readback for capture/testing, outside the normal frame loop. Web views already share a GPU device, but still need a GPU-only multi-pass contract.
2. **General model G-buffer and deferred lighting.** The formal model renderer now supports Forward / Deferred, using shared material sampling and PBR lighting in `model.hlsl`. Its four-target G-buffer stores base/roughness, mapped and unmapped normals, split view depth/occlusion, and emissive/metallic. It supports instances, skinning and debug views; see [the raster API](../api/raster). Instance identity and correct previous instance transforms still need to be added for effect histories. Hybrid now samples the GPU output texture directly; Raster retains its legacy capture/presentation path. Queue completion is synchronized and hardware ray-query acceleration structures are not yet shared.
3. **Secondary-ray effects.** Reuse the shared HLSL BSDF/light code, software BVH, Metal queries and Vulkan fallback. Launch rays from raster surfaces for soft shadows, AO and roughness-aware reflections. Integrate contributions without double-counting raster direct light or environment/probe reflections. Preserve per-light semantics for collections rather than applying one shadow factor to all lights.
4. **Temporal reconstruction.** Add effect history, reprojection, disocclusion/depth/normal/instance rejection, moments/variance and edge-aware spatial filtering. Existing display bilateral denoising and color TAA alone do not provide this. Handle cuts, resize/viewport changes and moving lights explicitly.
5. **DDGI.** Add probe placement, ray budgets, irradiance and distance moments, border updates, visibility weighting, hysteresis and recursive bounce feedback. Existing reflection Cubemap probes are specular environment caches, not DDGI irradiance probes. Update probes incrementally on compute-BVH backends and record coverage/convergence, not just frame count.
6. **Reference ingestion and integration.** Build offline glTF/OBJ/TGA conversion and coverage handling, capture presets, example UI and debug views, capabilities, native/Web tests and SDK packaging. Continue DXC -> SPIR-V -> Metal/WGSL; no reference framework is required as a consumer runtime dependency.

Likely new implementation modules: `GBuffer`, `DeferredLighting`, `HybridSession/View`, shared ray-scene HLSL, effect-history filtering and `DDGI`. Native backend adapters and the Web orchestrator both change. The existing two rendering modes remain regression targets. This is appropriate for a feature release, with implementation staged below; source-line or schedule estimates before the first GPU-sharing prototype would be speculative.

## Resource audit

The official archive is 205,799,803 bytes (about 206 MB decimal); its 181 entries unpack to 394,431,478 bytes. It is cached under ignored `build/reference/hybrid-rendering/`. Its SHA-256 and all camera frames are pinned in `tests/fixtures/hybrid-reference/manifest.json`.

The actual source scenes are:

- Shadows Test: 12 pillar instances, one bunny and one ground, 147,130 assembled triangles. Preserve both pillar rows and the bunny's translation, Y rotation and scale.
- Reflections Test: `reflections_test.gltf`, 234,124 triangles.
- Global Illumination Test: `global_illumination_test.gltf`, 15,864 triangles.
- Pica Pica: `scene.gltf`, 76,274 triangles. This is the file loaded by the program, not the other similarly named glTF in the archive.
- Sponza: `sponza.obj` / MTL, 262,267 triangles, with a world scale of 0.3. This is not the current lightweight Intel Sponza AO asset.

Each geometry fits the current 128 MiB expanded-scene vertex budget. Texture atlasing/storage-buffer budgets still need measurement; geometry capacity does not establish texture capacity.

Ingestion gaps are real: Nova accepts embedded GLB with OPAQUE/MASK/weighted OIT BLEND materials; the archive contains external-buffer glTF, OBJ/MTL and TGA textures. Pica includes a BLEND glass material and Sponza has opacity maps. The reference G-buffer disables blending and discards alpha below 0.1, so its compatibility coverage policy must be recorded explicitly rather than silently treating everything as opaque. General transparency beyond that reference policy is a separate rendering concern.

A specific import difference is already confirmed in the pinned `dwSampleFramework` revision `be5c6230ee85092cdd966056675c7f797a739ae6`: its `Mesh::load` reads raw `aiMesh` vertices and does not traverse node transforms. Nova correctly applies glTF nodes. A standards-preserving GLB repack therefore rotates the reference ground/pillars/bunny by roughly 90 degrees relative to the sample. `scripts/reference/prepare_hybrid.py --profile reference-import` explicitly reproduces the reference's raw-mesh convention in cached test assets; `--profile gltf` retains the original glTF hierarchy. Neither changes Nova's importer. The helper embeds the original image bytes, checks the pinned archive hash and uses only Python's standard library. UV orientation still needs visual comparison because the reference enables Assimp `FlipUVs`.

The five opaque meshes for the first three feature scenes have been packaged and read by Nova's actual Web GLB importer without changing triangle counts. This establishes import feasibility, not frame rendering or hybrid acceptance. Sponza and Pica coverage/conversion remain a later ingestion step.

Reference lighting also needs a formal parameter mapping. Its GI-test spot preset uses intensity 1000, above Nova's current 100 limit, and its shadow radii exceed the current softness mapping. Preserve the reference scene/camera units; specify a compatible intensity range and explicit radius semantics before implementation. Reference artistic gains (for example indirect-specular strength and recursive DDGI boosts) are not universal physical calibration constants.

The reference code is MIT licensed; its license is retained beside the fixture. `scene.gltf` embeds SEED.EA attribution and CC-BY-NC-4.0. No model or texture is added to the Git repository or SDK. Test resources remain separately fetched from the pinned archive; archive availability does not establish a common redistribution license for every asset.

## Acceptance contract

The fixture pins all **five original scenes and twenty fixed cameras** from `src/common.cpp`, including forward/right vectors and derived Nova `[x,y,z,w]` camera orientations. Use the source's 60-degree vertical FOV, near/far 1/1000 and a declared aspect ratio. The default application starts in the Shadows Test with a free camera; the repository screenshots do not identify a fixed-camera index.

Acceptance proceeds at three levels:

1. **Import/framing:** same reference-imported mesh geometry, instance assembly, world transforms, materials/coverage and each of four fixed cameras per scene. Do not substitute a white proxy or change the camera to improve appearance. Check landmarks and depth/normal views before judging lighting.
2. **Hybrid effects:** capture final, direct/shadow, AO, reflection, DDGI and motion/history views. Demonstrate contact and soft shadows, off-screen reflected objects, roughness response, indirect color transfer and response to a moving light. Compare Raster, Hybrid and high-sample RayTracing at the same camera/material/light/environment settings. DDGI is approximate; do not use pixel identity with PT as the acceptance rule.
3. **Temporal/performance:** record raw and filtered frames during a reproducible camera motion and cut, probe update coverage, GPU timings where supported, frame time, memory and ray execution type. Match per-probe convergence budgets across native and Web profiles. Capture quality at a proposed 1280x720; establish performance targets after profiling instead of promising hardware-RT frame rates for compute BVH.

The same asset package also includes four HDR environments. A reproducible comparison must pin an explicit environment, exposure, light profile, AA and warm-up/probe-coverage state. The default reference procedural sky needs its own matching implementation or a declared shared HDR preset; it is not equivalent to Nova's existing generic sky.

Metal uses hardware queries. macOS Vulkan and WebGPU can use compute BVH with smaller update budgets and accurate capability reporting. The original executable is a Windows Vulkan hardware-RT program: it has not been run on this Mac. Original-program real-GPU comparisons remain pending the already-deferred Windows machine; source/camera extraction and archive inspection are not rendering acceptance.

## Implementation stages

1. Import and validate original assets/cameras; produce a raster geometry/material baseline and decide reference light/coverage settings.
2. Add shared GPU resource ownership, real G-buffer and deferred composition; validate direct-light parity with Raster.
3. Add secondary-ray shadows/AO/reflections and temporal reconstruction, with paired C++/Web capabilities and debug outputs.
4. Add DDGI and its contribution to diffuse lighting and reflected hits; validate the original GI scene before the larger scenes.
5. Run all twenty views plus temporal captures on Metal/Vulkan/WebGPU, regress the existing modes, and only then package the complete Hybrid mode and update the editor. Windows runtime remains deferred unless the device becomes available.
