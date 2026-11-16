# Nova Render Web

The browser adapter lives in `web/src/Nova.js` and is also available as the [npm archive](../guide/package). Its lifecycle corresponds to the native API, with asynchronous JavaScript loading and drawing.

## Usage

```js
import * as Nova from '../src/Nova.js'

const renderer = await Nova.createRenderer({ canvas })
try {
  await renderer.setShader(shader)
  renderer.setClearColor([0.025, 0.035, 0.055, 1])
  await renderer.drawFrame()
} catch (error) {
  console.error(error)
} finally {
  renderer.dispose()
}
```

The runnable source is `web/examples/triangle.js`; the documentation executes and displays that same file. Package consumers import `@nova/render-web` and set their deployed asset directory.

## Methods

- `createRenderer({canvas, shaderDirectory})`: creates an adapter, device and canvas context; failures reject the Promise.
- `setShader(shader)`: accepts a precompiled `{source, vert, frag}` package. Passing only `{source}` requires the local HLSL compilation service. Entry points are `VSMain` and `PSMain`.
- `setClearColor([r,g,b,a])`: sets four finite values in [0,1].
- `drawFrame()`: synchronizes canvas pixel dimensions and submits work; returns `rendered` or `skipped` for zero dimensions.
- `waitIdle()`: waits for submitted GPU work.
- `dispose()`: releases the context and resources. Repeated disposal is safe; drawing afterwards is invalid.

Await asynchronous operations. Loading and drawing are serialized per instance; wait for loading before editing dependent settings. Compilation, pipeline and device-loss errors reach the caller. This supports triangles and embedded, opaque multi-material GLB models, including skeletal animation. See [Scene instances](./scene) and [Materials](./materials) for the formal model APIs.

`loadModel`, `setCamera`, `getCamera` and `enableOrbitControls` provide [model and camera interaction](./model). `setLighting('unlit' / 'pbr')` defaults to unlit; `setDirectionalLight` / `getDirectionalLight` use `direction`, `color`, and `intensity`.

`enableParameterPanel` / `disableParameterPanel` manage the optional [panel](./ui). `loadEnvironment` / `setEnvironment` / `getEnvironment` manage [HDR lighting](./environment). Controls redraw automatically and are disposed with the renderer.

## Rendering

[Raster and ray tracing](./render-mode#web-usage) share parameter ranges with C++. The browser uses GPU compute BVH traversal, without WASM or dedicated hardware ray-query APIs. [Animation](./animation) currently supports raster rendering only.

Ordinary raster examples expose `setAntiAliasing({method:'taa'})`, `getAntiAliasing()` and `getAntiAliasingHistoryFrames()`. Default AA is off; see [anti-aliasing](./antialiasing). [Post-processing](./postprocessing) uses the same shared HLSL chain.

Published Pages examples load precompiled WGSL. HLSL editing is enabled only in the local development server; JavaScript/C++ source tabs are read-only examples.

See [Camera and viewport](/api/camera) for `setViewCamera`, `getViewCamera`, `setViewport` and `getViewport`.

See [scene picking](./picking) for synchronous instance ray queries and hit data.

`getCapabilities().metropolis` reports MLT availability in current source builds. WebGPU compute BVH supports MLT; native DX12 WARP is excluded. See [device limits](./render-mode#windows-device-limits).
