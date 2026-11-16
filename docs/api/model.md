# Models and cameras

C++ and web follow the same flow: load a model, set a camera, draw. By default the result is base-color texture × base-color factor, without lighting, occlusion, emission or exposure. Parsing, uploads, depth and platform graphics calls are encapsulated.

For complete position/quaternion orientation, projection, lens and viewport control, use the [camera and viewport API](./camera). The examples below retain the compatible orbit controller.

## C++

```cpp
render::Window window;
window.LoadModel("assets/DamagedHelmet/DamagedHelmet.glb");
render::OrbitCamera camera;
camera.yaw = 20;
camera.pitch = 5;
window.SetCamera(camera);
window.EnableOrbitControls();
while (window.PollEvents()) window.DrawFrame();
window.WaitIdle();
```

Renderer also exposes `LoadModel` / `SetCamera` for externally managed windows. Without a model it draws the built-in triangle; load failure throws.

```sh
cmake --build build --target nova_helmet
./build/nova_helmet assets/DamagedHelmet/DamagedHelmet.glb --api metal
```

## Web

```js
const renderer = await Nova.createRenderer({ canvas, shaderDirectory: '/generated/' })
await renderer.loadModel('/generated/DamagedHelmet.glb')
renderer.setCamera({ yaw: 20, pitch: 5, distance: 3.6, exposure: 1 })
await renderer.drawFrame()
renderer.enableOrbitControls()
```

`model.json` is precompiled at build time; runtime loading needs no DXC, Rust or compilation server. Angles are degrees. Default camera: yaw 0, pitch 0, distance 3.6, exposure 1; pitch is ±85°, distance 1.5–100, exposure (0,16]. Models are centered by their bounding box and normalized to longest side 2.

## Lighting

[Model loading](../examples/damaged-helmet) is unlit; [lighting](../examples/lighting) explicitly enables PBR.

```cpp
window.SetLighting(render::LightingMode::PBR);
render::DirectionalLight light;
light.direction = {-1, 1.6f, 2};
light.color = {1, 0.9f, 0.8f};
light.intensity = 3.5f;
window.SetDirectionalLight(light);

```

```js
renderer.setLighting('pbr')
renderer.setDirectionalLight({ direction: [-1, 1.6, 2], color: [1, 0.9, 0.8], intensity: 3.5 })

await renderer.drawFrame()
```

The main direction is finite and nonzero (length at least 1e-6), then normalized. Color is linear RGB [0,1]; intensity is [0,100]. Defaults are direction `[-1,1.6,2]`, color `[1,3.2/3.5,0.8]`, intensity 3.5. Invalid settings preserve the previous state. Zero disables the main light, but helper/environment lighting and emission remain; unlit mode ignores these controls. Exposure affects PBR and skyboxes, not unlit base color.

Native light options: `--light-direction X Y Z --light-color R G B --light-intensity N`.

## Mouse controls

Enable `Window::EnableOrbitControls()` or browser `enableOrbitControls({onChange,onError})` for left-drag orbit and wheel zoom. Native events are processed by `PollEvents()`; the application keeps drawing. Web controls capture pointers, normalize wheel units and redraw automatically. `rotateSpeed` defaults to 0.35 degrees/pixel and `zoomSpeed` to 0.001; both must be positive and finite.

`GetCamera()` / `getCamera()` return copies. Programmatic camera changes require a draw. Re-enabling controls does not duplicate listeners; disable them explicitly or dispose the renderer. Scrolling outside the canvas remains normal page scrolling.

## Scope

Embedded, opaque, triangle GLB models use one core material with POSITION and TEXCOORD_0; indexed and non-indexed data are accepted. Missing normals are reconstructed. [GPU skeletal animation](./animation) is supported, but morph targets, transparency and compressed extensions are not. Model switching preserves camera and lighting.

`examples/common/ModelOptions.hpp` parses native arguments. Auto uses the platform mapping; `--spin` is optional automated camera rotation, and `--frames N` exits after N frames.

Cornell variants are generated test scenes, not calibrated replicas of Cornell's measurements. A front view uses yaw 0, pitch 0, distance 3.6. Emissive-panel lighting in ray tracing is sampled through paths.

## Ray-traced glass

The ray loader accepts `KHR_materials_transmission.transmissionFactor` in [0,1] and `KHR_materials_ior.ior` in [1.01,3], default 1.5. Materials may differ in these extensions while sharing core PBR factors and texture atlases. Transmission textures are unsupported; roughness uses the metallic-roughness texture with a glass floor of 0.06.

Use closed, outward-facing geometry for air/glass interfaces. Shared HLSL implements Fresnel, Snell refraction, total internal reflection and radiance/importance transport corrections. Absorption, dispersion and nested media are unsupported; rasterization uses an opaque fallback. See `assets/CornellCaustics` and [PBRT dielectric BSDFs](https://pbr-book.org/4ed/Reflection_Models/Dielectric_BSDF).
