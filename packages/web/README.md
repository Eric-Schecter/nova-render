# Nova Render Web — @nova/render-web

Local development package. Install the generated `.tgz`, then copy its runtime assets:

```sh
npm install /path/to/nova-render-web-0.1.0.tgz
npx nova-web-assets --out public/nova
```

```js
import { createRenderer, createReflectionProbeView } from '@nova/render-web'
const renderer = await createRenderer({ canvas, shaderDirectory: '/nova/' })
const shader = await fetch('/nova/triangle.json').then(r => r.json())
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
