# Weekend Spheres

Original procedural geometry and textures created for Nova, inspired by the random spheres final scene in [Ray Tracing in One Weekend](https://raytracing.github.io/books/RayTracingInOneWeekend.html). No downloaded assets or book source code are included.

Rebuild: `node scripts/generate-weekend-spheres.mjs`.

Deterministic seed 20261114; 134 spheres, 53788 triangles, diffuse / metallic atlas and closed glass (transmission 1, IOR 1.5), ground plane and an overhead area emitter. The finite emitter enables emitter-only PT/BDPT/photon/MLT comparisons. This is an adaptation, not a pixel reproduction: triangulated spheres, finite ground and area light; no analytic sphere intersection or depth of field. Existing native and web model loaders use the same GLB and shared HLSL.

Native example: run `nova_environment assets/WeekendSpheres/WeekendSpheres.glb assets/Environment/venice_sunset.nenv --mode ray`.
