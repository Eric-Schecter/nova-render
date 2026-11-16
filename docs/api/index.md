# API reference

Native headers live in `render/include/render`; the browser module exports corresponding asynchronous JavaScript APIs.

- [Window](./window): application window, events and presentation.
- [Renderer](./renderer): lower-level externally managed window integration.
- [Nova Render Web](./web): WebGPU lifecycle and shader loading.
- [Types and configuration](./types), [backend selection](./selection).
- [Models and cameras](./model), [skeletal animation](./animation), [environment lighting](./environment).
- [Render modes and integrators](./render-mode), [ambient occlusion](./ambient-occlusion).
- [Anti-aliasing](./antialiasing), [shadows](./shadows), [post-processing](./postprocessing).
- [Screen-space reflections](./ssr), [planar reflections](./planar), [reflection probes](./probe).
- [Parameter panels](./ui).

See [writing an example](../guide/integration) and [installing packages](../guide/package). Dedicated laboratory views document their own fixed-scene limitations; not every effect is integrated into arbitrary-model Renderer paths.

- [Scene instances](/api/scene): shared static GLBs, world transforms and original model space.

- [Camera and viewport](/api/camera): complete camera pose, projection, lens and framebuffer rectangles.

- [Scene picking](/api/picking): CPU ray queries, instance filters and world-space hit data.

- [Materials](/api/materials): multiple slots and runtime colors, PBR factors and textures.
