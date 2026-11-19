# Materials

Use native libraries, headers and web shader assets from the same build.

`Renderer` and `Window` expose the same metallic/roughness materials as WebGPU. A GLB can contain multiple material slots; primitives keep their glTF material indices, including when one slot is shared by several primitives. Animation, AA, post-processing and single-model ray tracing use the updated materials through shared HLSL.

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
- `baseColor`: linear RGBA, each component 0–1; default white. Alpha multiplies base-color texture alpha for MASK and BLEND; OPAQUE ignores it.
- `alphaMode`: C++ `AlphaMode::Opaque / Mask / Blend`; JavaScript `'OPAQUE' / 'MASK' / 'BLEND'`. Default OPAQUE.
- `alphaCutoff`: 0–1, default 0.5. MASK discards pixels below this threshold before color/depth writes.
- `doubleSided`: default false. Back faces are discarded; double-sided back faces flip both geometric and mapped normals. Mirrored instances and negative baked node transforms preserve the authored front side.
- `roughness`, `metallic`: 0–1, both default 1. Lighting must be PBR to see their effect.
- `specularFactor`: dielectric reflection strength, 0–1, default 1. `specularColorFactor`: non-negative linear RGB multiplier for dielectric F0, default white. The metal reflection is unchanged.
- `clearcoatFactor` and `clearcoatRoughnessFactor`: clear coat strength and roughness, each 0–1 and default 0. The top layer has a separate dielectric reflection over the base material.
- `normalScale`: 0–16, default 1. `occlusionStrength`: 0–1, default 1.
- `emissive`: linear RGB, 0–65504, default black.
- `baseColorTexture`, `normalTexture`, `metallicRoughnessTexture`, `occlusionTexture`, `emissiveTexture`: optional RGBA8 images with `width`, `height`, `pixels`. Each dimension is 1–8192. Base color and emissive maps are sRGB; other maps are linear. Metallic/roughness uses B/G channels, occlusion uses R. Missing maps use white, or a neutral tangent-space normal.

AO uses `occlusionTexCoord` (0 by default), which selects `TEXCOORD_0` or `TEXCOORD_1` in Forward, Deferred and Hybrid. The GLB importer reads `occlusionTexture.texCoord` and requires the specified coordinates on the primitive. When a runtime material selects UV1 on geometry without UV1, sampling falls back to UV0. Other core texture maps use `TEXCOORD_0`. UV accessors support float and normalized unsigned byte or short, with repeat sampling. Multiple slots share an atlas: tiles are capped at 2048 pixels and automatically reduced to fit the 8192-pixel / 512 MiB material atlas budget; at most 128 slots per packed model/scene. A single-slot model retains its original image resolution. Compression and transmission texture editing are not included.

GLB loading supports the two factor properties of [KHR_materials_specular](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_specular). They affect Forward, Deferred and Hybrid PBR lighting on native and WebGPU. Assets using either specular texture property are rejected. Ray mode uses its existing material lighting.

The same pipelines support [KHR_materials_clearcoat](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_clearcoat). The coating responds to scene lights and environment reflections. In Hybrid, the base layer can use traced reflections and the coat samples the environment. Hybrid secondary hits sample all three maps. Standalone Ray mode samples the same clearcoat maps on opaque materials. Its integrators share a GGX coat lobe, a matched mixture PDF, and Fresnel attenuation of the underlying BSDF and emission. The coat is a thin layer without its own refraction. The standalone path-tracing alpha restriction still applies.

- `clearcoatTexture` multiplies the strength factor by its linear red channel.
- `clearcoatRoughnessTexture` multiplies the roughness factor by its linear green channel.
- `clearcoatNormalTexture` perturbs only the coat normal using linear RGB in a tangent frame derived from geometry and UVs. `clearcoatNormalScale` scales its tangent-plane components and defaults to 1.
- `clearcoatTexCoords` defaults to `[0, 0, 0]` and selects UV0 or UV1 independently for strength, roughness, and normal. The importer reads each map's `texCoord` field. Runtime edits use the same material slot setters as base material edits.

## Update behavior

Setters validate factors, slot IDs and image storage before replacing material data. Invalid input preserves the last successful state. Loading a new model/scene replaces its slots; transform-only scene updates preserve edits. Geometry, source-space bounds and picking results stay unchanged. Animation time is retained, and temporal AA/ray accumulation resets after an edit.

Runtime edits currently rebuild and upload the model's material resources; they are intended for inspector changes rather than per-frame material animation. Image decoding is separate from replacement; await the image helper and setter before drawing. Web URLs follow normal fetch/CORS rules.

Specialized procedural laboratory views own their internal surfaces and do not expose these model slots. Static scene instances support the same material fields.

## Live example

<ClientOnly><MaterialPlayground /></ClientOnly>

## Alpha rendering and capabilities

Forward and Deferred use OPAQUE/MASK depth writes and a separate weighted blended OIT pass for BLEND. OIT blends without draw sorting across slots and instances; it approximates layered transparency and does not simulate glass refraction. It composites in the current working color space before enabled post-processing. G-buffer diagnostics show only opaque/masked surfaces.

`GetMaterialCapabilities()` / `getMaterialCapabilities()` reports raster `opaque`, `mask`, `blend`, `doubleSided` and `maskShadows` as true. MASK shadows use factor × base-mip texture alpha. BLEND casts no scene shadow (`blendShadows: false`). MASK/BLEND ray tracing, ray material sidedness and alpha-aware picking are false; picking remains a two-sided geometry query. Loading or editing alpha materials in Ray mode, or switching such a scene to Ray, fails explicitly.

BLEND supports Off, FXAA, SMAA and SSAA. `blendMSAA`, `blendTAA` and `blendAASplit` are false; incompatible setters fail before replacing state. MASK supports the normal raster AA choices; Deferred MSAA remains unavailable. `transparency` is `weighted-blended-oit`.

See [Alpha material validation](../examples/material-alpha).
