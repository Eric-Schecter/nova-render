# Raster pipelines

Switch between **Forward**, **Deferred**, **Forward+** and **Clustered Forward** with the same camera, opaque geometry and up to 256 local lights. Drag to orbit; scroll to zoom. Open Parameters to select the pipeline or inspect intermediate data.

<ClientOnly><PipelinePlayground /></ClientOnly>

Forward shades geometry against every light. Deferred writes four G-buffer targets and shades visible surfaces in a full-screen pass. Forward+ uses a depth prepass and 16 × 9 screen tiles; Clustered Forward divides those tiles into 16 logarithmic depth slices. Both build compact GPU light lists before shading geometry.

Output includes base color, world-space normal, linear view depth, roughness, metallic, evaluated light count and list overflow. Deferred reads its actual G-buffer; forward paths visualize the equivalent interpolated surface data without adding a G-buffer pass.

This is a standalone opaque raster laboratory, separate from `RenderMode`. It uses fragment passes to build the light-list atlas for portability through the existing native/Web raster infrastructure. Its culling implementation is instructional, not an optimized compute benchmark. Cells exceeding 64 lights fall back to all lights; magenta in List overflow identifies these cells. There are no shadows, transparency or AA/post effects in this example, keeping pipeline comparisons focused. [API and data layout](../api/pipelines).
