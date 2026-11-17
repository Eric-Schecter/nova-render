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

Range and the near-source inverse-square clamp apply to the first source-to-surface segment; subsequent bounces transport that energy normally. Point/spot lights remain ideal point sources: `shadowSoftness` filters direct visibility, not photon emission or physical source size. Static snapshots, finite bounce limits, and photon density bias still apply. Per-path radiance is not clipped; tone mapping and denoising remain display operations.

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

## 0.7.1 dielectric energy correction

The former roughly 16% glass-spot energy discrepancy was primarily an implementation error, not evidence of inherent photon-map bias. Camera NEE rejected transmission hemispheres; BDPT delta-source visibility offset the shadow ray toward the eye and self-occluded transmitted connections. Both now use the full dielectric BSDF and offset toward the outgoing light direction. A hidden per-path radiance ceiling of 64 was also removed because it clipped rare caustic samples and broke intensity linearity. The documented near-source inverse-square regularization remains unchanged.

The non-emissive 32-by-32 glass-spot fixture uses a 32,768-spp BDPT reference and 4,096 camera samples with 8,192 photons per iteration. Metal/Vulkan means are approximately 0.00078289 (BDPT) and 0.00078162 (photons, initial radius 0.16), a 0.16% difference; WebGPU reports about 0.17%. Independent PT and a radius sweep from 0.03 to 0.32 pass a 3% plus four block-standard-error agreement bound. These are fixture-specific finite-sample measurements, not a universal error guarantee.

Single-interface regressions verify point/spot transmission, signed visibility bias, real blockers and high-intensity linearity above the old clipping threshold. Tests are `dielectric-lights-*`, `caustic-energy-*`, `scripts/webgpu/check-dielectric-lights.mjs` and `scripts/webgpu/check-caustic-energy.mjs`. The source-light regression now requires at most 5% mean-energy disagreement and improved raw reconstruction MSE in both Cornell fixtures. Photon density estimates still have finite-radius bias and individual realizations need not converge monotonically.
