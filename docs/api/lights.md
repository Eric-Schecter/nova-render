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

This release supports up to **16 total lights**, in any combination, on **Metal, Vulkan, DX12 and WebGPU raster** paths. The limit includes disabled entries. `directional`, `point`, `spot` and `maxLights` describe collection support for the current backend and mode. Ray tracing retains its legacy single directional light; collection setters throw in that mode. Switching to ray tracing while a collection is active throws; restore the legacy light first.

`directionalShadows`, `pointShadows` and `spotShadows` are true on supported raster backends. Shadows require opaque static `SetScene` geometry and the global shadow switch (`shadowRequiresScene` is true). Every enabled light with `castShadow: true` casts its own BVH shadow. Point/spot rays end at the light source, so objects behind the source do not occlude it. Range and spot-cone attenuation are applied before shadow visibility.

Zero `shadowSoftness` uses one hard visibility ray. Positive values use the global sample count (1–16): directional sources retain a disk radius of `softness * .006` radians; point/spot sources use a disk centered at the light position with radius `softness * .006` **world units**. This is a visibility filter; attenuation and cone evaluation use the central source and it is not a full area-emitter model. Changing light type preserves the numeric setting but changes its units. The global bias is in world units, including under nonuniform instance scale. No transparent/alpha-tested or animated-caster shadows are provided by this static-scene path.

`SetDirectionalLight` / `setDirectionalLight` exits collection mode and restores the compatible single-light rendering path. `GetLights` then reports one `__directional` entry. Built-in parameter panels do not overwrite application-owned collections; edit collections through the API or your application UI.

## 0.5.0 validation

Metal and Vulkan (MoltenVK on macOS) pass matching hard/soft point/spot shadow tests, global and per-caster switches, caster movement, behind-source rejection, range/cone exclusion and mixed-light shadow toggles. WebGPU passes matching pixel checks through the same HLSL-to-WGSL chain. Relocated Metal/Vulkan SDK and standalone npm consumers exercise the formal shadow capabilities. DX12 shaders compile to DXIL, but Windows hardware/WARP runtime and native SDK validation remain **deferred** at the user's request. No Windows SDK is published in 0.5.0. Ray-tracing light collections remain unsupported.
