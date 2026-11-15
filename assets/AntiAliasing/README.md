# AA laboratory resources

The scene is generated from vertex IDs by Nova's shared `antialiasing.hlsl`; no model download is required.

SMAA 1x uses the official SMAA 2.8 HIGH preset, its three reconstruction passes and original Area/Search tables from https://github.com/iryoku/smaa (downloaded 2026-11-15). The MIT-style notice is retained in both table headers and `render/shaders/third_party/smaa/LICENSE.txt`.

`SMAA.hlsl` has two integration changes: Latin-1 text is stored as UTF-8; its inline sampler declarations are replaced by Nova's explicit linear/point bindings. Algorithm and table contents remain the reference implementation.

`render/shaders/third_party/fxaa/FXAA.hlsli` is NVIDIA FXAA 3.11 by Timothy Lottes, reused from the original Nova project. The original copyright/disclaimer is retained. The wrapper uses the HLSL 4 sampling path, quality preset 29 and gamma luminance in alpha; this avoids a DXC/Naga ImageGather translation limitation.

MSAA is a real 4-sample raster target plus hardware resolve. SSAA renders at twice the width and height and resolves 2x2 shaded samples in linear color. TAA is Nova's implementation with eight-phase Halton jitter, per-vertex current/previous transforms, motion/depth/material reprojection rejection and neighborhood clamping; it is not a port of a vendor TAA SDK.
