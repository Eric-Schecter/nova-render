# CarConcept showroom

The showroom uses a neutral studio HDRI with two rectangular softboxes, a low front-side fill, layered Carmine paint and baked tire contact shadows. Drag to orbit or scroll to zoom. Run the native example from the repository root with `./build/nova_showroom --api metal` or `--api vulkan`.

Hybrid uses a 5 × 4 × 5 DDGI grid around the car and nearby floor on Metal, Vulkan and WebGPU. **Show DDGI probes** overlays the probe positions, colored by height. The desktop toggle is available in UI-enabled builds. The native example falls back to per-pixel diffuse bounce when the backend lacks probe support. While orbiting or zooming, the showroom uses two samples for the top softbox shadow. It restores four after the camera settles.

<ClientOnly><ModelPlayground showroom /></ClientOnly>
