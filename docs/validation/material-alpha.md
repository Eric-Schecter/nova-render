# Alpha material validation

Self-authored fixtures in `assets/MaterialSample` cover overlapping layers, a binary-alpha checker, back faces, mirrored nodes and multi-slot GLBs. Regenerate them with `node scripts/generate-material-alpha-sample.mjs`.

Native `nova_material_alpha_test` and `scripts/webgpu/check-material-alpha.mjs` check OPAQUE ignoring alpha, MASK factor × texture alpha and cutoff, double-sided faces, negative node/instance scale, draw-order invariance, Forward/Deferred parity, runtime updates, invalid-update preservation, AA restrictions and cutout scene shadows. Browser captures use the compositor, then compare with native captures.

Verified on Apple M4: Metal, Vulkan through MoltenVK, Chrome WebGPU. Maximum native/Web mean absolute channel error: 0.084 / 255 across 28 matching captures. Native validation layers reported no GPU errors. Windows DX12 shader compilation is checked separately; Windows runtime validation remains deferred.

BLEND is weighted blended OIT, not exact sorted alpha or transmission. MASK/BLEND ray tracing and alpha-aware picking are explicitly unavailable. The live [example](../examples/material-alpha) uses the same public material API as applications.
