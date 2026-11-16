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
