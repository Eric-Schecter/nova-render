# Alpha materials

Overlapping translucent layers, a cutout texture and front/back-facing planes verify material coverage. Open Material controls to change a slot at runtime. The mirrored fixture tests a negative node transform.

<ClientOnly><MaterialAlphaPlayground /></ClientOnly>

::: code-group

```cpp
window.LoadModel("assets/MaterialSample/Layers.glb");
auto material = window.GetMaterials().at(1);
material.alphaMode = render::AlphaMode::Blend;
material.baseColor[3] = 0.45f;
material.doubleSided = true;
window.SetMaterial(1, material);
```

```js
await renderer.loadModel('/generated/MaterialLayers.glb')
const material = renderer.getMaterials()[1]
material.alphaMode = 'BLEND'
material.baseColor[3] = 0.45
material.doubleSided = true
await renderer.setMaterial(1, material)
```

:::

BLEND uses weighted blended OIT, an order-independent approximation without glass refraction. MASK tests factor × texture alpha and supports cutout scene shadows. Alpha materials currently require Raster mode; BLEND supports Off, FXAA, SMAA and SSAA. [Material API](../api/materials) · [Validation](../validation/material-alpha).

Native: `nova_material_alpha assets/MaterialSample/Layers.glb --api metal` (or `vulkan`). The Materials panel uses the same slot, alpha, sidedness, lighting and pipeline settings.
