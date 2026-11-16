# Parameter panels

Optional viewport UI uses Dear ImGui on C++ and plain HTML/CSS on web, without WASM or an extra frontend framework. Groups and ranges correspond across platforms. Integrator comparison alone places controls below its four images; other examples use collapsible top-right overlays. AO and subsequent native demos also use floating panels.

## C++

```cpp
render::Window window;
window.LoadModel("assets/DamagedHelmet/DamagedHelmet.glb");
window.SetLighting(render::LightingMode::PBR);
window.EnableOrbitControls();
window.EnableParameterPanel();
while (window.PollEvents()) window.DrawFrame();
window.WaitIdle();
```

`Renderer::EnableParameterPanel(bool enabled=true)` also works; false hides it. The panel is off by default and examples opt in. A false return means UI was excluded from the build. The main application loop draws it; no background thread or extra native window is created.

Use `-DNOVA_ENABLE_UI=OFF` to omit ImGui. Enabled builds obtain ImGui and backend bindings through vcpkg; SDKs include libraries and notices.

## Web

```js
renderer.enableOrbitControls()
renderer.enableParameterPanel({ onError: (error) => console.error(error) })
// renderer.disableParameterPanel()
```

`enableParameterPanel()` returns cleanup and does not duplicate panels. `dispose()` removes DOM, listeners and pending UI frames. Controls call the existing model/camera/light APIs and schedule redraws. Using the standalone module does not require Vue.

## Controls

- Scene: dark/light clear color.
- Camera: yaw ±180°, pitch ±85°, distance 1.5–100 and reset.
- PBR/environment: exposure 0.1–3. Main light: azimuth ±180°, elevation ±90°, linear RGB [0,1], intensity [0,100], reset.
- Environment: sky visibility, rotation ±180°, lighting/sky intensity [0,16], reset. Clear-color presets are disabled while the sky is shown.

UI input is isolated from orbit interaction. Hide-preview also hides its panel. `SetClearColor({r,g,b,a})` / `setClearColor([r,g,b,a])` use finite [0,1] display sRGB values; light colors remain linear. Getters return copies.

## Model and environment choices

Application-owned lists supply display names and resource paths/URLs. Enabling a panel does not load the first entry; set the initial asset with the load methods. Omitted lists hide selectors. Switching keeps camera and lighting settings; load failure preserves the old resource.

```cpp
render::ParameterPanelConfig panel;
panel.models = {
    {"Avocado", "assets/Avocado/Avocado.glb"},
    {"BoomBox", "assets/BoomBox/BoomBox.glb"},
    {"Cornell Box", "assets/CornellBox/CornellBox.glb"}
};
panel.environments = {
    {"Venice Sunset", "assets/Environment/venice_sunset.nenv"},
    {"Studio Small 09", "assets/Environment/studio_small_09.nenv"}
};
window.EnableParameterPanel(panel);
```

Native `AssetChoice` has `name` and `path`. Requests are applied before the next frame. The browser loads asynchronously, disables selectors while loading, restores failed selections and reports through `onError`.

```js
renderer.enableParameterPanel({
  models: [
    { name: 'Avocado', url: '/generated/Avocado.glb' },
    { name: 'BoomBox', url: '/generated/BoomBox.glb' },
    { name: 'Cornell Box', url: '/generated/CornellBox.glb' },
  ],
  environments: [
    { name: 'Venice Sunset', url: '/generated/venice_sunset.bin' },
    { name: 'Studio Small 09', url: '/generated/studio_small_09.bin' },
  ],
  onError: (error) => console.error(error),
})
```

[Animation](./animation) adds clip, playing, loop, speed and time controls. [Render modes](./render-mode) provide Raster / Ray tracing and integrator settings. [Integrator comparison](../examples/integrators) owns separate fixed-sample/active-time budgets, freezes on completion, and deliberately disables external lighting and denoising.

Panel lifecycle and input isolation have been exercised on Metal, Vulkan, Ubuntu Lavapipe, Chrome WebGPU and a desktop-session Windows DX12 WARP VM; this is not a claim of Windows hardware-GPU validation.
