# Skybox and environment lighting

One resource provides both sky background and PBR environment lighting. Native and web share settings, preprocessed data and HLSL.

## C++

```cpp
window.LoadEnvironment("assets/Environment/venice_sunset.nenv");
render::EnvironmentSettings environment;
environment.rotation = 30;
environment.intensity = 1;
environment.backgroundIntensity = 1;
environment.showBackground = true;
window.SetEnvironment(environment);

```

Enable `LightingMode::PBR` for models to receive environment lighting. Skyboxes can render without a model and do not move with camera distance. Renderer exposes the same load/get/set methods as Window.

## Web

```js
await renderer.loadEnvironment('/generated/venice_sunset.bin')
renderer.setEnvironment({
  rotation: 30,
  intensity: 1,
  backgroundIntensity: 1,
  showBackground: true,
})
await renderer.drawFrame()
```

Read a copy using `getEnvironment()`, edit it, then call `setEnvironment()` with a complete configuration. Failed loading preserves the old environment. Programmatic changes require a draw; UI changes redraw automatically.

## Settings

- `rotation`: world Y rotation, −180–180 degrees, default 0; sky and reflections stay aligned.
- `intensity`: environment lighting multiplier, 0–16, default 1.
- `backgroundIntensity`: sky brightness, 0–16, default 1, independent of model lighting.
- `showBackground`: default true; hiding the sky does not disable lighting.

Values must be finite; invalid settings preserve state. Camera exposure affects both sky and PBR. A loaded HDR replaces the default analytic studio helper; the main directional light can still be added. Example 4 defaults its main light intensity to zero.

## Resources

Native `.nenv` resources contain a separate 512×512 six-face background cube with spatial mipmaps, plus a 256×256 GGX-prefiltered lighting cube and diffuse spherical harmonics. Both preserve linear RGBA16F HDR. Split-sum specular approximation does not itself provide shadows or dynamic capture.

The optional `nova_environment_bake` tool converts Radiance HDR; see `assets/Environment/README.md`. It reuses stb/GLM and is not built by default. Runtime consumers need no HDR decoder or shader compiler. New resources are about 20 MiB each and remain compatible with older `NOVENV01` files; the web representation uses `.bin`.

[Example 4](../examples/environment) provides Venice Sunset, Kloppenheim 06 and Studio Small 09. [Asset lists](./ui#model-and-environment-choices) belong to the application. Sources and licenses are in [credits](../credits).
