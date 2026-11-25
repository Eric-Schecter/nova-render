<script setup>
import DeviceCapabilities from '../.vitepress/theme/DeviceCapabilities.vue'
</script>

# Device capabilities

<ClientOnly><DeviceCapabilities /></ClientOnly>

The browser reports the WebGPU adapter features and limits that are visible to this page. Adapter identifiers can be omitted by the browser.

Build and run the native example from a configured desktop build directory with examples enabled.

```sh
cmake --build build --target nova_capabilities
./build/nova_capabilities --api vulkan
```

Use `--api metal`, `--api dx12`, or `--api auto` for another supported backend. The native example reports Nova's ray tracing, Hybrid, raster, material, animation and lighting capabilities through `Window` and lists hardware capabilities for the selected API. The device section enumerates adapters separately. It shows reported capabilities before Nova's device feature selection.

- **Materials and Bindless** — sampled textures per stage and per set, descriptor indexing or binding tier, texture dimensions, storage-buffer ranges, and memory budget. Check actual texture count and residency separately from the number of material records.
- **Deferred and AA** — color attachments, target format and sample count. An advertised sample count does not guarantee that a specific Nova pipeline supports it.
- **GPU-driven rendering** — indirect draw support, draw count, compute threadgroup limits, and workgroup memory.
- **Ray tracing and Hybrid** — check both hardware or API support and Nova's feature flags, including software ray tracing, probe GI, and supported scene features.
- **Texture compression and memory** — supported formats, dedicated versus unified memory, and memory budgets where exposed. A physical heap size alone does not represent free memory.
