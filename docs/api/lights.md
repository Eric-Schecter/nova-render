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
window.SetLights({point});
```

```js
renderer.setLighting('pbr')
renderer.setLights([
  { id: 'key', type: 'point', position: [1, 2, 3], color: [1, .7, .4], intensity: 8, range: 10 },
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

This release supports up to **16 total lights**, in any combination, on **Metal and WebGPU raster** paths. The limit includes disabled entries. `directional`, `point`, `spot` and `maxLights` describe collection support for the current backend and mode. Vulkan, DX12 and ray tracing retain their legacy single directional light; collection setters throw there. Switching to ray tracing while a collection is active throws; restore the legacy light first.

`directionalShadows` is true on supported raster backends. Shadows require opaque static `SetScene` geometry and the global shadow switch (`shadowRequiresScene` is true). Every enabled directional light can cast its own BVH shadow with its own direction/softness. `pointShadows` and `spotShadows` are false: setting their `castShadow` to true is rejected, including disabled entries. No point/spot shadow fallback is fabricated.

`SetDirectionalLight` / `setDirectionalLight` exits collection mode and restores the compatible single-light rendering path. `GetLights` then reports one `__directional` entry. Built-in parameter panels do not overwrite application-owned collections; edit collections through the API or your application UI.
