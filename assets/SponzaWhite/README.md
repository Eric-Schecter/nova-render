# Intel Sponza — lightweight white model

Source: [Intel GPU Research Samples, Sponza Base Scene](https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-research/samples.html), download `https://cdrdv2.intel.com/v1/dl/getContent/830833` (`main_sponza.zip`). Scene commissioned by Frank Meinl, sponsored by Anton Kaplanyan (2022).

The downloaded package's complete `credits_license.txt` is included unchanged. It contains attribution, usage notes and the CC BY 4.0 text; retain the complete file when redistributing this derivative. This asset is not CC0. No Crytek/Khronos Sponza files are used.

Nova modifications: extract only glTF geometry (no textures, lights or other source formats), bake node transforms, weld positions, simplify with meshoptimizer 1.3.0 (relative error limit 0.003), merge into one white matte material, discard textures/UVs/normals. Flat normals are reconstructed by the loaders. The initial target is 180,000 triangles; the error limit preserves more detail, producing **251,936 triangles, 144,925 indexed vertices, 4,763,176 bytes (4.76 MB / 4.54 MiB)**. The original scene contains 3,747,018 triangles. Checksums, bounds and simplification settings are in `manifest.json`.

Only ~99 MB of compressed geometry/license data was transferred using HTTP Range, rather than downloading the full 3.99 GB ZIP (listed by Intel as 3.71 GB). The large source geometry and temporary offline tool are not runtime dependencies or shipped assets.

Rebuild after extracting `NewSponza_Main_glTF_003.gltf`, its `.bin` and `credits_license.txt` to a source directory:

```sh
python3 scripts/fetch-sponza-geometry.py /tmp/nova-sponza-source
npm install --prefix /tmp/nova-asset-tools meshoptimizer@1.3.0
NOVA_MESHOPT_MODULE=/tmp/nova-asset-tools/node_modules/meshoptimizer/index.js \
  node scripts/prepare-sponza.mjs /tmp/nova-sponza-source 180000
```

The existing renderer needs no new runtime decoder or texture dependency. Camera target is specified in the normalized model coordinates; AO radius also uses these units. A radius of 0.08 is about 1.45 m in this source scene.
