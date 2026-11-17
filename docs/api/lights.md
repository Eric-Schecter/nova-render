# Scene lights

`Renderer` and `Window` expose `SetLights(vector<SceneLight>)`, `GetLights()` and `GetLightingCapabilities()`. Web uses `setLights`, `getLights` and `getLightingCapabilities`. Use PBR lighting to see direct lights.

```cpp
render::SceneLight point;
point.id = "key";
point.type = render::LightType::Point;
point.position = {1, 2, 3};
point.color = {1, .7f, .4f};
point.intensity = 8;
point.range = 10;
window.SetLighting(render::LightingMode::PBR);
point.castShadow = true;
window.SetShadows({true});
window.SetLights({point});
```

```js
renderer.setLighting('pbr')
renderer.setShadows({ enabled: true, samples: 8 })
renderer.setLights([
  { id: 'key', type: 'point', position: [1, 2, 3], color: [1, .7, .4], intensity: 8, range: 10, castShadow: true },
  { id: 'rim', type: 'spot', position: [-2, 3, 0], direction: [0, -1, 0],
    innerConeAngle: .35, outerConeAngle: .6, intensity: 12 },
])
```

Collections replace all direct lights atomically. IDs must be unique, nonempty UTF-8 strings of at most 128 bytes. Reads return copies. `enabled` defaults to true. An empty collection removes direct lighting, including the legacy studio fill; environment lighting remains independent. Changing lights resets temporal AA and transport accumulation without reloading geometry.

- `type`: `Directional`, `Point`, `Spot` in C++; lowercase strings on web. Default: directional.
- `color`: linear RGB in [0,1], default white. `intensity`: Nova relative multiplier in [0,100], default 1, not lumens or candela.
- `position`: world-space XYZ, default zero. Used by point/spot lights.
- `direction`: normalized by the SDK. Directional points **toward the source**; spot points **outward from the source**. Default [0,1,0]. A finite nonzero vector is required for directional/spot lights.
- `range`: nonnegative world-space distance, default 0 (unlimited). Point/spot attenuation is `1 / max(distance², .01)`, multiplied by `saturate(1 - (distance/range)^4)^2` for positive range.
- `innerConeAngle`, `outerConeAngle`: spot half angles in radians, defaults .35 and .6; `0 <= inner <= outer < pi/2`. Smoothstep falloff between cones; equal angles give a hard cone.
- `castShadow`: default false. `shadowSoftness`: [0,4], default 1. Global `SetShadows` remains the shadow master switch and controls bias/sample count.

Invalid values, duplicate IDs and excess counts throw before replacing the previous collection. Disabled entries still count toward the limit and are validated. Only type-applicable position/direction/range/cone fields are validated.

## Capabilities and scope

This release supports up to **16 total lights** in raster and ray-tracing modes on Metal, Vulkan, DX12 and WebGPU. Disabled entries count toward the limit. Collection state survives mode switches, and changes reset progressive accumulation without re-uploading geometry. Empty collections remove the legacy direct light while leaving environment/emission intact.

Ray tracing supports point/spot **BDPT source subpaths and photon emission**, including scenes with no emissive mesh. `collectionLightSubpaths` and `collectionPhotonEmission` are true in ray mode; `sourceLightTypes` is `[point, spot]`. `estimator` reports `raster-pbr` or `camera-and-source-paths`. These fields describe available estimators, not the currently selected integrator. PT uses camera-path NEE, BDPT combines eye and light subpaths with MIS, and photon mapping combines direct NEE with indirect photon density estimates. MLT uses the same BDPT target for indirect transport. Directional lights and HDR illumination retain camera-path estimation.

Point sources emit over the sphere; spot sources sample their outer cone and weight the smooth inner/outer falloff. Position is a discrete delta, never an area PDF. Source selection probabilities and emission-direction PDFs normalize light-path throughput and photon flux. Mixed emissive geometry and local lights share emission selection, with no duplicate camera-path local-light term. The `lightSampling` switch still controls PT area/HDR MIS.

Range and the near-source inverse-square clamp apply to the first source-to-surface segment; subsequent bounces transport that energy normally. Point/spot lights remain ideal point sources: `shadowSoftness` filters direct visibility, not photon emission or physical source size. Static snapshots, finite bounce limits, photon density bias, and the existing radiance clamp still apply.

Select the estimator through the existing ray settings; no light-specific backend calls are needed:

```cpp
auto settings = window.GetRayTracing();
settings.integrator = render::PathIntegrator::Bidirectional; // or PhotonMapping
window.SetRayTracing(settings);
window.SetRenderMode(render::RenderMode::RayTracing);
```

```js
renderer.setRayTracing({ ...renderer.getRayTracing(), integrator: 'bidirectional' })
// Use 'photon-mapping' for photon emission and density reconstruction.
renderer.setRenderMode('ray-tracing')
```

`directionalShadows`, `pointShadows` and `spotShadows` are true on supported raster and ray backends. Both obey per-light cast flags and the global shadow switch. Raster shadows require opaque static `SetScene` geometry (`shadowRequiresScene` is true); ray tracing accepts static single models and multi-instance scenes (`shadowRequiresScene` is false). Every enabled light with `castShadow: true` casts its own BVH shadow. Point/spot rays end at the light source, so objects behind the source do not occlude it. Range and spot-cone attenuation are applied before shadow visibility.

Zero `shadowSoftness` uses one hard visibility ray. Raster positive values use the global sample count (1–16); progressive ray tracing uses one stochastic visibility sample per vertex per sample: directional sources retain a disk radius of `softness * .006` radians; point/spot sources use a disk centered at the light position with radius `softness * .006` **world units**. This is a visibility filter; attenuation and cone evaluation use the central source and it is not a full area-emitter model. Changing light type preserves the numeric setting but changes its units. The global bias is in world units, including under nonuniform instance scale. No transparent/alpha-tested or animated-caster shadows are provided by this static-scene path.

`SetDirectionalLight` / `setDirectionalLight` exits collection mode and restores the compatible single-light rendering path. `GetLights` then reports one `__directional` entry. Built-in parameter panels do not overwrite application-owned collections; edit collections through the API or your application UI.

## 0.7.0 validation

Metal, Vulkan and WebGPU pass six-light linear additivity, disabled/empty collections, point range and spot cones, shadow toggles, accumulation reset, four integrators, multi-instance transforms and runtime materials. Vulkan prefers hardware queries and falls back to shared-HLSL compute BVH when unavailable; `softwareRayTracing` truthfully reports the fallback. macOS Vulkan validation uses this compute path; Metal uses native acceleration structures. DX12 shaders compile to DXIL, but Windows hardware/WARP runtime and native SDK validation remain deferred at the user's request. No Windows SDK is published in 0.7.0.

Non-emissive Cornell and glass-caustic scenes audit point/spot source energy against independent high-sample references, diffuse photon convergence, glass-caustic energy, disabled/range-limited sources and native/Web parity. The source sampler and all kernels are shared HLSL. See the renderer tests `source-lights-metal`, `source-lights-vulkan` and `scripts/webgpu/check-source-lights.mjs`.

For sharp glass caustics the validation reports, without asserting monotonic improvement, both raw pixel MSE and 4-by-4 block-integrated MSE against an independent BDPT reference. Shrinking photon kernels retain finite-budget density bias: pixel MSE is not guaranteed to decrease monotonically. The supplied glass spot fixture still shows a roughly 16% mean-energy difference at its finite test budget; this is not a claim of pixel-identical or unbiased photon reconstruction.
