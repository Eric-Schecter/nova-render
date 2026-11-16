# Render modes

Window, Renderer and web expose corresponding APIs. Default Mode is Raster; loaded static models can switch to Ray tracing, then choose PT / BDPT / Photon mapping / MLT inside the Integrator setting. Comparison is an example, not a Mode.

```cpp
render::Window window;
window.LoadModel("assets/DamagedHelmet/DamagedHelmet.glb");
window.LoadEnvironment("assets/Environment/venice_sunset.nenv");

const auto capabilities = window.GetCapabilities();
if (capabilities.rayTracing)
{
    render::RayTracingSettings settings;
    settings.samplesPerFrame = 1;
    settings.maxBounces = 3;
    settings.resolutionScale = 0.5f;
    settings.lightSampling = true;
    window.SetRayTracing(settings);
    window.SetRenderMode(render::RenderMode::RayTracing);
}
window.EnableParameterPanel();
while (window.PollEvents())
    window.DrawFrame();
window.WaitIdle();
```

## Controls

- `GetCapabilities()`: `rayTracing`, `softwareRayTracing`, `metropolis`, `rayTracingDevice` and unavailable `reason`.
- `SetRenderMode(Raster / RayTracing)` / `GetRenderMode()`: unavailable or model-less ray tracing throws without changing the mode.
- `SetRayTracing` / `GetRayTracing`: `samplesPerFrame` 1–4, `maxBounces` 1–8, `resolutionScale` 0.25–1; invalid values throw.
- `GetAccumulatedSamples()` and `ResetAccumulation()`: progress and restart.

Camera, model, light, environment, clear color and size changes restart accumulation. Switching to raster restores raster material/settings.

```sh
./build/nova_environment assets/DamagedHelmet/DamagedHelmet.glb \
  assets/Environment/venice_sunset.nenv --api metal \
  --mode ray --spp 1 --bounces 3 --scale 0.5 --integrator path
```

Model, lighting and environment examples support these flags. Use `--api dx12` on Windows, `--api vulkan` on Linux; Windows `--warp` requests software validation.

## Backends and scope

DX12 uses DXR 1.1 inline queries; Vulkan uses acceleration structures/ray-query extensions; Metal uses native acceleration structures/ray queries. DXC compiles HLSL to DXIL/SPIR-V, SPIRV-Cross produces Metal. Disable native tracing with `NOVA_ENABLE_PATH_TRACING=OFF`.

Supports static models, limited rough glass, base-color/normal/metallic-roughness/emission maps, directional shadows, HDR environments, bounces and progressive accumulation. Core factors/atlases must be shared between supported material variants. Path radiance is clamped to 64 to limit fireflies. No absorption, dispersion, nested media or ray-traced skinning.

Native tracing currently reads results back to CPU and uploads for display, adding transfer/synchronization overhead. Browser compute/display stays on GPU.

## Web usage

```js
await renderer.loadModel('/generated/DamagedHelmet.glb')
renderer.setRayTracing({
  samplesPerFrame: 1,
  maxBounces: 3,
  resolutionScale: 0.5,
  lightSampling: true,
})
renderer.setRenderMode('ray-tracing')
await renderer.drawFrame()
renderer.enableParameterPanel()
```

Use lowercase counterparts for capability/mode/ray settings, reset and accumulation. WebGPU builds a JavaScript BVH and traverses it in a compute shader, not a dedicated hardware ray API. `softwareRayTracing: true` here means GPU compute intersection, not CPU rendering; device label is `WebGPU compute BVH`. Materials/sampling share HLSL with native.

The first draw starts continuous accumulation. Hiding the preview, leaving the viewport or backgrounding the tab pauses it; raster/disposal stops it. Storage buffers must fit browser limits. Legacy PathTracing-named settings/methods and `'path-tracing'` input remain aliases; the canonical getter returns `'ray-tracing'`.

## Light sampling and MIS

`lightSampling`, default true, controls PT next-event estimation. Emissive triangles are chosen by area × luminance; HDR sampling accounts for luminance and solid angle. Shadow rays test visibility; a power heuristic combines light and diffuse/GGX BSDF sampling. Visible lights/skies keep their brightness; directional lights are sampled separately. Disabling it gives a BSDF-only comparison. Extra samples/rays do not guarantee faster frames.

## Bidirectional path tracing

```cpp
auto settings = window.GetRayTracing();
settings.integrator = render::RayIntegrator::Bidirectional;
window.SetRayTracing(settings);
```

```js
renderer.setRayTracing({ ...renderer.getRayTracing(), integrator: 'bidirectional' })
```

Finite emissive surfaces start light subpaths, connected to camera paths using area-measure probabilities and MIS. BDPT always uses MIS; the PT toggle does not disable it. HDR/directional lighting keep unidirectional NEE/MIS, and scenes without finite area emitters use that estimator. No camera splats to other pixels. See [PBRT BDPT](https://pbr-book.org/3ed-2018/Light_Transport_III_Bidirectional_Methods/Bidirectional_Path_Tracing).

A recorded Cornell test (48×48, five bounces, no environment/directional light/denoising) reduced equal-spp error roughly 76% at 8 spp and 67% at 32 spp versus a 2048 spp PT reference, with high-sample mean brightness difference about 0.19%. This does not prove faster convergence per second. Windows software tests use WARP 1.0.22 beside test executables; older system WARP crashed in this kernel. No system DLL is replaced.

## Photon mapping

```cpp
auto settings = window.GetRayTracing();
settings.integrator = render::RayIntegrator::PhotonMapping;
settings.photonsPerIteration = 8192;
settings.photonRadius = 0.16f;
window.SetRayTracing(settings);
```

```js
renderer.setRayTracing({
  ...renderer.getRayTracing(),
  integrator: 'photon-mapping',
  photonsPerIteration: 8192,
  photonRadius: 0.16,
})
```

`photonsPerIteration`: 256–65536, default 8192. `photonRadius`: 0.03–0.5 normalized world units, default 0.16. Camera samples in one iteration share the photon map. A bounded linked grid gathers density on diffuse visible points; metallic/glossy surfaces continue camera paths. Direct lighting is estimated separately to avoid double counting. Photon emission restarts each iteration; radius shrinks with iteration to the power −1/6 before averaging pixel estimates.

Finite radius smooths illumination with bias, independently of display denoising. Photon emission starts at area lights; HDR/directional/no-area-light cases retain the other estimator. See [integrator comparison](../guide/integrator-comparison).

## Denoising

```cpp
auto settings = window.GetRayTracing();
settings.denoise = true;
window.SetRayTracing(settings);
```

```js
renderer.setRayTracing({ ...renderer.getRayTracing(), denoise: true })
await renderer.drawFrame()
```

`denoise` defaults false. Shared HLSL performs a 5×5 depth/normal/luminance-guided bilateral display filter, excluding sky. It never modifies raw radiance history. Toggling restarts accumulation. This is spatial filtering, without AI or temporal history; fireflies may remain, textures may soften, and missing low-resolution detail cannot be recovered. Animated models currently use raster only.

## Offscreen views and comparison

```cpp
#include <render/RayTracingView.hpp>
render::RayTracingView view;
view.LoadModel("assets/CornellCaustics/CornellCaustics.glb");
render::RayTracingSettings settings;
settings.integrator = render::RayIntegrator::Metropolis;
view.SetRayTracing(settings);
const render::Image& frame = view.DrawFrame(256, 256);
window.PresentImage(frame);

```

`RayTracingView` owns one offscreen accumulator without a window/UI. Image is sRGB RGBA8 with width/height/pixels. `PresentImage` copies it; `ClearImage` restores Window output. Images accept dimensions 1–8192 and exactly width×height×4 bytes. Minimized windows report zero framebuffer size.

Web `setContinuousRendering(false)` allows application-managed draws. `Nova.attachOrbitControls(element,{getCamera,setCamera,drawFrame})` supports shared multi-view interaction and returns disposable controls.

Example 6 owns four independent views in a 2×2 grid: PT, BDPT, Photon mapping, MLT; identical camera/scene, area lighting only, denoising disabled. Budgets/stats/below-image UI belong to C++ `example::ComparisonScene` and `web/examples/comparison.js`, not core Mode settings.

`SetBudget({64,0})` / `Progress()` correspond to `setBudget({samples:64,milliseconds:0})` / `getProgress()`. Samples: 1–65536; time: 0–60000 ms, with nonzero time taking priority. Each iteration is 1 spp per view (1 mutation/pixel for MLT). Timing includes first-draw setup, submission and GPU completion/readback, but excludes earlier device/model loading and waiting for other views. Final iterations may exceed the time budget. Relevant state changes restart the comparison.

## Metropolis light transport

```cpp
auto settings = window.GetRayTracing();
settings.integrator = render::RayIntegrator::Metropolis;
settings.maxBounces = 8;
settings.mltBootstrapSamples = 262144;
settings.mltLargeStepProbability = 0.3f;
settings.mltMutationScale = 0.01f;
settings.mltSeed = 1;
window.SetRayTracing(settings);
window.SetRenderMode(render::RenderMode::RayTracing);
```

```js
renderer.setRayTracing({
  ...renderer.getRayTracing(),
  integrator: 'mlt',
  maxBounces: 8,
  mltBootstrapSamples: 262144,
  mltLargeStepProbability: 0.3,
  mltMutationScale: 0.01,
  mltSeed: 1,
})
renderer.setRenderMode('ray-tracing')
```

- `mltBootstrapSamples`: 256–1048576, default 262144.
- `mltLargeStepProbability`: 0.05–1, default 0.3.
- `mltMutationScale`: 0.0001–0.25, default 0.01.
- `mltSeed`: uint32, default 1; changing it reinitializes chains.

This is PSSMLT over BDPT primary sample space, not MMLT or specialized path-space mutations. Direct light is separate; chains estimate paths with at least two scattering events. Bootstrap estimates indirect mean luminance and brightness-resamples up to 1024 chains; each warms up 16 mutations. Accepted/rejected proposals both contribute weighted splats, while rejection preserves the complete old sample vector. See [PBRT MLT](https://pbr-book.org/3ed-2018/Light_Transport_III_Bidirectional_Methods/Metropolis_Light_Transport).

Accumulation reports average mutations/pixel (mpp), not independent camera spp. Bootstrap/warmup are excluded from mpp but included in first-iteration time. Correlated noise and fixed-bootstrap normalization error remain; more mpp alone does not remove bootstrap error. Recheck brightness with larger bootstrap counts or different seeds. Shared HLSL uses paired uint32 fixed-point accumulation/carry rather than float atomics, within eight storage bindings. Parameter/camera/scene changes restart; display denoising is optional but comparison disables it.

## Windows device limits

Current source builds expose `metropolis` in C++ and Web capabilities. Check it before selecting MLT. DX12 WARP supports the other validated ray integrators, but MLT is disabled: initialization can remove the software device. The API rejects it before GPU submission and the native panel omits it. The four-integrator comparison requires MLT support. WebGPU compute traversal still supports MLT; its `softwareRayTracing` flag describes GPU traversal, not WARP.

The `metropolis` capability flag is included in 0.2.1 and later; the original v0.2.0 archives predate it.
