# Camera and viewport

On Web, `setRenderScale(scale)` scales both framebuffer dimensions after applying the device pixel ratio. Use a value greater than 0 and at most 1. `getRenderScale()` reports the active scale, which defaults to 1. Changing the scale resets temporal history and ray accumulation. The canvas layout and camera are independent of the framebuffer scale.

Available on `main` for the next SDK version; not included in v0.1.0. Native headers, libraries and web shader assets must come from the same build.

The core `Renderer` / `Window` and WebGPU `createRenderer` support independent camera position, quaternion orientation and lens settings. These APIs apply to static/animated model rasterization, scene instances, AA, post-processing and the core single-model ray tracer. Specialized laboratory views keep their existing orbit interfaces.

## Camera

C++: `SetViewCamera(const Camera&)`, `GetViewCamera()`.

JavaScript: `setViewCamera(settings)`, `getViewCamera()`. JavaScript accepts partial updates; C++ accepts the complete struct. Getters return copies. Setters validate before replacing the active camera. Changed camera/viewport settings reset temporal history and ray accumulation; repeating identical settings preserves history.

- `position`: world-space `[x,y,z]`; default `[0,0,3.6]`.
- `orientation`: local-to-world quaternion `[x,y,z,w]`; default identity `[0,0,0,1]`. Nonzero quaternions are normalized. Full roll and exact top/bottom views are supported.
- `projection`: `Projection::Perspective` / `'perspective'` (default), or `Projection::Orthographic` / `'orthographic'`.
- `verticalFov`: vertical field of view in degrees, 1–179; default 45. Used for perspective projection.
- `orthographicHeight`: full vertical span in world units, at least `1e-5`; default 3. Moving the camera does not change orthographic scale.
- `nearPlane`, `farPlane`: positive finite distances along the camera's forward axis, with `nearPlane < farPlane`; defaults 0.01 and 200.
- `exposure`: positive value up to 16; default 1. Unlit base color retains its existing exposure-independent behavior.

The coordinate system is right-handed: camera-local +X is right, +Y up and -Z forward. Quaternions use glTF's `[x,y,z,w]` order. Viewport aspect ratio determines horizontal FOV/span; it is not a second camera setting.

```cpp
render::Camera camera;
camera.position = {0, 2, 6};
camera.orientation = {0, 0, 0.38268343f, 0.9238795f}; // 45-degree roll
camera.verticalFov = 60;
camera.nearPlane = 0.1f;
camera.farPlane = 100;
window.SetViewCamera(camera);

camera.projection = render::Projection::Orthographic;
camera.orthographicHeight = 5;
window.SetViewCamera(camera);
```

```js
renderer.setViewCamera({
  position: [0, 2, 6],
  orientation: [0, 0, 0.38268343, 0.9238795],
  verticalFov: 60,
  nearPlane: 0.1,
  farPlane: 100,
})
renderer.setViewCamera({ projection: 'orthographic', orthographicHeight: 5 })
```

Ray tracing uses the same camera pose and projection. Near/far planes clip the primary camera segment, including geometry behind a clipped front surface; they do not clip secondary light transport. Orthographic PBR and sky backgrounds use parallel view directions.

## Viewport

C++: `SetViewport(std::optional<Viewport>)`, `GetViewport()`.

JavaScript: `setViewport(rectangleOrNull)`, `getViewport()`.

A rectangle contains `x`, `y`, `width`, `height`. All values are uint32 integers; width and height must be positive. Coordinates use the **framebuffer's top-left origin in physical pixels**, consistently across backends. JavaScript does not implicitly interpret them as CSS pixels.

```cpp
window.SetViewport(render::Viewport{80, 60, 640, 480});
window.DrawFrame();
window.SetViewport(std::nullopt); // Restore the automatic full viewport.
```

```js
renderer.setViewport({ x: 80, y: 60, width: 640, height: 480 })
await renderer.drawFrame()
renderer.setViewport(null)
```

Rectangles are intersected with the current framebuffer on each frame; the resulting width/height sets the projection aspect and AA/post-processing target size. A fully outside rectangle suspends drawing (`Skipped` / `'skipped'`) until it intersects again. Zero-size rectangles are rejected; use a suspended/zero-size render target to suspend the whole window.

The output outside an active rectangle is cleared to the renderer's clear color. AA, post-processing and ray tracing stay inside the rectangle. There is one scene viewport per renderer; this is not an API for preserving multiple views by repeatedly drawing onto one target. UI overlays still use the full window/canvas. The optional native `sceneTopInset` remains the automatic fallback; an explicit viewport overrides it.

`GetViewport` returns the configured rectangle, not the clamped result. Resetting it restores automatic resizing. For browser UI measured in CSS pixels, multiply coordinates and dimensions by the canvas backing-store scale (`canvas.width / canvas.clientWidth`, similarly for height). Recompute a pixel rectangle when the layout or device-pixel ratio changes.

## Orbit compatibility

Existing `SetCamera(OrbitCamera)` / `setCamera(orbitSettings)` remain orbit-controller interfaces. `GetCamera` / `getCamera` returns their last orbit settings; use `GetViewCamera` / `getViewCamera` for the effective complete camera.

`CameraFromOrbit` / exported `cameraFromOrbit` converts orbit settings to a complete camera. Calling the legacy setter or moving a legacy orbit control returns to orbit mode and its legacy lens defaults. Setting a full camera on `Window` or the browser renderer disables attached orbit navigation; custom camera controllers should update the complete camera directly. The bare native `Renderer` does not own input callbacks.

For upright cameras, call `Window::EnableViewOrbitControls(distance)` or `enableOrbitControls({ viewDistance: distance })` after setting the full camera. The controller places the pivot `distance` units along the current view direction and preserves projection, FOV, clipping planes and exposure during drag and zoom. Distance must be in [1.5, 100].

The built-in parameter panel remains an orbit-oriented convenience panel. A custom editor should drive the complete camera API instead of approximating roll, orthographic height or FOV through orbit distance.

<ClientOnly><CameraPlayground /></ClientOnly>
