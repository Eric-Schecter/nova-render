# Nova Render Web — @nova/render-web

Install the precompiled release archive, then copy its runtime assets:

```sh
npm install https://github.com/Eric-Schecter/nova-render/releases/download/v0.5.0/nova-render-web-0.5.0.tgz
npx nova-web-assets --out public/nova
```

```js
import { createRenderer, createReflectionProbeView } from '@nova/render-web'
const renderer = await createRenderer({ canvas, shaderDirectory: '/nova/' })
const shader = await fetch('/nova/triangle.json').then((r) => r.json())
await renderer.setShader(shader)
await renderer.drawFrame()
// renderer.dispose() on teardown
```

Serve `public/` with your application. For subpath deployments, pass the actual asset
base URL through `shaderDirectory` (include a trailing slash). The asset-copy command
only copies package-owned files and does not remove unrelated files in the destination.
All renderer factories use the same public API as the examples. Assets include WGSL
and SMAA tables; no DXC, SPIRV-Cross, shader server or runtime npm dependencies are needed.
This package uses JavaScript and WebGPU, without WASM. Use HTTPS or localhost and a
WebGPU-capable browser/device. Models and HDR environment assets are application-owned.

Only precompiled shader loading works without a shader server: passing HLSL-only to
`setShader()` still requires a separately configured compilation service. Production
apps should pass the packaged `vert`/`frag` shader object or use the built-in view factories.

No public distribution license has been assigned (`UNLICENSED`). Third-party notices
are in `licenses/`. The generated package is private to prevent accidental publishing.

## Static scene instances

```js
await renderer.setScene(
  [{ id: 'model', url: '/model.glb', sourceSpace: true }],
  [
    { id: 'first', assetId: 'model' },
    { id: 'second', assetId: 'model', world: [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 2, 0, 0, 1] },
  ],
)
const bounds = renderer.getSceneAssetBounds('model')
await renderer.drawFrame()
```

Instances share resources by asset ID. `sourceSpace: true` retains authored glTF units and origin; the default normalizes the model. Matrices are column-major; omitted matrices use identity. `setScene([], [])` clears geometry. This API supports static opaque raster scenes; see the [scene API](https://eric-schecter.github.io/nova-render/api/scene.html) for limits.

## Camera and viewport

Use `setViewCamera({ position, orientation, projection, verticalFov, orthographicHeight, nearPlane, farPlane })` for complete pose/lens settings. `setViewport({x,y,width,height})` uses framebuffer pixels; `setViewport(null)` restores the automatic viewport. Existing `setCamera(orbit)` remains available.

See [camera and viewport](https://eric-schecter.github.io/nova-render/api/camera.html) for defaults, validation and examples.

## Scene picking

`renderer.pickScene({origin:[0,0,5],direction:[0,0,-1]})` synchronously returns a hit or `null` for the committed static scene.

Optional `includeIds` / `excludeIds` filter instance IDs; exclusions win. Hits contain `id`, world `position`, unit geometric `normal` and world-space `distance`. [Picking API](https://eric-schecter.github.io/nova-render/api/picking.html).

Materials: read `GetMaterials()` / `getMaterials()`, edit a slot, then call `SetMaterial(slot, material)` / `await setMaterial(slot, material)`. Pass the scene asset ID as the final argument to edit all its instances. Texture maps accept RGBA8 images; `LoadMaterialImage(path)` / `await loadMaterialImage(url)` decodes an image. See the materials API documentation for ranges and atlas limits.
