# CarConcept showroom

The **Lighting** selector defaults to **Quarry daylight**, based on the Google Autos demo's outdoor HDRI and directional light. Switch to **Warm side** to compare the warm softbox and cool overhead fill, or to **Studio** for the original studio lighting. The presets share the camera, layered Carmine paint, floor and baked tire contact shadows. Quarry uses the HDRI for lighting and reflections against a neutral backdrop. Drag to orbit or scroll to zoom. Run the native example from the repository root with `./build/nova_showroom --api metal` or `--api vulkan`.

Quarry daylight uses environment intensity 0.6, directional intensity 0.68 and +1.39 EV post exposure. These values match the UE outdoor reference on an 18% horizontal gray card with the display curves bypassed. The combined linear luminance measured 0.222 in Nova and 0.219 in UE. The studio presets use their original exposure.

The showroom blends 25% scene-traced specular with 75% environment specular on the car. The floor material uses 100% scene-traced specular to show a clear reflection of the car.

Hybrid uses a 5 × 4 × 5 DDGI grid around the car and nearby floor on Metal, Vulkan and WebGPU. **Show DDGI probes** overlays the probe positions, colored by height. The desktop toggle is available in UI-enabled builds. The native example falls back to per-pixel diffuse bounce when the backend lacks probe support. While orbiting or zooming, the showroom uses two samples for the top softbox shadow. It restores four after the camera settles.

<ClientOnly><ModelPlayground showroom /></ClientOnly>

The Web preview renders at 80% of its framebuffer resolution while orbiting or zooming and restores full resolution when the camera settles.
