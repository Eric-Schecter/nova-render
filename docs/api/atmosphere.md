# Scene atmosphere and application integration

`Renderer` / `Window` expose `SetFog(FogSettings)` and `GetFog()`; WebGPU uses `setFog(settings)` and `getFog()`. Fog applies to model/scene raster surfaces, including unlit materials, AA and post-processing. The background and path tracer are unaffected.

- `mode`: C++ `FogMode::Off`, `Linear`, `ExponentialSquared`; JavaScript `'off'`, `'linear'`, `'exp2'`.
- `color`: linear RGB in 0–1; default `[0.3185468, 0.3185468, 0.3185468]`.
- `nearDistance`, `farDistance`: finite view-space distances with `0 <= nearDistance < farDistance`; defaults 10 and 50.
- `density`: 0–1, default 0.02, used for exponential-squared fog.

JavaScript accepts partial changes; getters return copies. Invalid input preserves the last valid state. Changed settings invalidate temporal history. Perspective and orthographic views use depth along the camera's forward axis.

```cpp
render::FogSettings fog;
fog.mode = render::FogMode::Linear;
fog.color = {0.4f, 0.5f, 0.6f};
fog.nearDistance = 5;
fog.farDistance = 30;
window.SetFog(fog);
window.LoadEnvironment({}); // Clear the HDR environment and restore studio lighting.
```

```js
renderer.setFog({ mode: 'linear', color: [0.4, 0.5, 0.6], nearDistance: 5, farDistance: 30 })
await renderer.loadEnvironment(null)
```

Clear an environment with an empty native path or a `null`/empty web URL. Existing geometry, scene IDs, camera and materials remain intact.

## Native application UI

`Window::SetCloseOnEscape(false)` lets an application own Escape handling. It does not suppress keyboard events.

`ParameterPanelConfig::configureUI` runs once with the optional ImGui context active, before its first GPU frame, for application fonts/style. `drawUI` submits UI every frame. `showBuiltinPanel=false` hides the built-in inspector. Defer renderer mutations until after the draw callback.

`sceneViewport` optionally returns `{x,y,width,height}` in UI logical pixels after `drawUI`. The renderer converts DPI and clips the scene to that rectangle; explicit `SetViewport` takes priority. Web applications own their DOM UI and use `setViewport` in framebuffer pixels. UI-disabled builds keep the renderer APIs available and return false when UI is requested.

## GPU scene shadows

`SetShadows(SceneShadowSettings)` / `GetShadows()` match `setShadows(settings)` / `getShadows()`. These shadows affect the legacy main directional light or each collection directional, point or spot light with `castShadow` enabled on opaque, static raster scene instances. Point/spot rays stop at their finite light source; per-light softness uses a world-space disk. They use shared HLSL BVH traversal on the GPU on Metal, Vulkan, DX12 and WebGPU; no hardware ray-query extension or CPU depth-map rasterization is required. Studio fill and environment lighting remain unshadowed. Single-model path tracing retains its own transport shadows.

- `enabled`: false by default.
- `softness`: 0–4, default 1; zero produces hard shadows. Positive values sample a directional light disk (0.006 radians per unit).
- `samples`: integer 1–16, default 8; soft-shadow quality/cost. One sample is hard.
- `bias`: world-space offset, 0.00001–0.1, default 0.002. Adjust for the scale of the scene.

WebGPU raster scenes can opt in with `setShadows({ halfResolution: true })`. The renderer traces one receiver per 2 × 2 region and reconstructs visibility using full-resolution depth and geometric normals. When neighboring samples do not match the surface, it traces that receiver at full resolution. Output resolution and sample count are independent. The default is `false`. This WebGPU path needs deferred-capable limits and falls back to the regular raster shadow path with MSAA, unsupported devices or non-raster modes.

Scene instances accept `castShadow` (default true). Setting it false removes that instance from shadow queries while leaving it rendered and pickable. BVHs are shared per asset, so transform and material changes retain local geometry. Nonuniform and mirrored world transforms use inverse-transformed rays. The packed BVH budget is 64 MiB, with the web device's storage-buffer limit checked before upload. Changing settings resets temporal history.

```cpp
render::SceneShadowSettings shadows;
shadows.enabled = true;
shadows.softness = 1;
shadows.samples = 8;
window.SetShadows(shadows);
```

```js
renderer.setShadows({ enabled: true, softness: 1, samples: 8 })
await renderer.setScene(assets, [
  { id: 'ground', assetId: 'floor', world: floorWorld, castShadow: false },
  { id: 'model', assetId: 'helmet', world: modelWorld },
])
```

Native render verification covers Metal and Vulkan on macOS; DX12 implementation shares the shader/resource path but still requires Windows runtime verification.
