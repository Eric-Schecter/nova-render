# Materials

Available on `main` for the next SDK version; not included in v0.1.0. Use native libraries, headers and web shader assets from the same build.

`Renderer` and `Window` expose the same opaque metallic/roughness materials as WebGPU. A GLB can contain multiple material slots; primitives keep their glTF material indices, including when one slot is shared by several primitives. Animation, AA, post-processing and single-model ray tracing use the updated materials through shared HLSL.

## Read and replace a slot

C++: `GetMaterials(assetId = {})`, `SetMaterial(slot, material, assetId = {})`.

JavaScript: `getMaterials(assetId = '')`, `await setMaterial(slot, material, assetId = '')`.

After loading a model, omit `assetId`. After `SetScene` / `setScene`, pass the asset ID: every instance referencing that asset changes together. Instance-specific overrides are not exposed. Getters return independent copies, including image pixels. Read, edit and replace one complete material; other slots remain unchanged. JavaScript fills omitted fields with defaults; omitted or `null` texture fields remove the map. C++ uses `std::optional<Image>`.

```cpp
#include <render/Material.hpp>

window.LoadModel("model.glb");
auto materials = window.GetMaterials();
auto material = materials.at(0);
material.baseColor = {0.8f, 0.2f, 0.1f, 1};
material.roughness = 0.35f;
material.metallic = 0.7f;
material.baseColorTexture = render::LoadMaterialImage("albedo.png");
window.SetMaterial(0, material);

// Scene assets use the same API.
auto sceneMaterial = window.GetMaterials("helmet").at(0);
sceneMaterial.normalTexture.reset();
window.SetMaterial(0, sceneMaterial, "helmet");
```

```js
import { loadMaterialImage } from '@nova/render-web'

await renderer.loadModel('/models/model.glb')
const material = renderer.getMaterials()[0]
material.baseColor = [0.8, 0.2, 0.1, 1]
material.roughness = 0.35
material.metallic = 0.7
material.baseColorTexture = await loadMaterialImage('/textures/albedo.png')
await renderer.setMaterial(0, material)

const sceneMaterial = renderer.getMaterials('helmet')[0]
sceneMaterial.normalTexture = null
await renderer.setMaterial(0, sceneMaterial, 'helmet')
```

## Material fields

- `name`: slot label; does not affect primitive assignment.
- `baseColor`: linear RGBA, each component 0–1; default white. Alpha is retained as material data; these pipelines remain opaque.
- `roughness`, `metallic`: 0–1, both default 1. Lighting must be PBR to see their effect.
- `normalScale`: 0–16, default 1. `occlusionStrength`: 0–1, default 1.
- `emissive`: linear RGB, 0–65504, default black.
- `baseColorTexture`, `normalTexture`, `metallicRoughnessTexture`, `occlusionTexture`, `emissiveTexture`: optional RGBA8 images with `width`, `height`, `pixels`. Each dimension is 1–8192. Base color and emissive maps are sRGB; other maps are linear. Metallic/roughness uses B/G channels, occlusion uses R. Missing maps use white, or a neutral tangent-space normal.

Only `TEXCOORD_0` and repeat sampling are supported. Multiple slots share an atlas: tiles are capped at 2048 pixels and automatically reduced to fit the 8192-pixel / 512 MiB five-map atlas budget; at most 128 slots per packed model/scene. A single-slot model retains its original image resolution. Compression, alpha blending and transmission texture editing are not included.

## Update behavior

Setters validate factors, slot IDs and image storage before replacing material data. Invalid input preserves the last successful state. Loading a new model/scene replaces its slots; transform-only scene updates preserve edits. Geometry, source-space bounds and picking results stay unchanged. Animation time is retained, and temporal AA/ray accumulation resets after an edit.

Runtime edits currently rebuild and upload the model's material resources; they are intended for inspector changes rather than per-frame material animation. Image decoding is separate from replacement; await the image helper and setter before drawing. Web URLs follow normal fetch/CORS rules.

Specialized procedural laboratory views own their internal surfaces and do not expose these model slots. Scene instances retain their existing raster-only limit.

## Live example

<ClientOnly><MaterialPlayground /></ClientOnly>
