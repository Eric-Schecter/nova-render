# Venice Sunset environment

Source: [Venice Sunset](https://polyhaven.com/a/venice_sunset) by Greg Zaal, Poly Haven.
License: [CC0](https://polyhaven.com/license).

`venice_sunset_1k.hdr` is the original Radiance HDR file. SHA-256:
`92fe2b29b1957828fb8925a57633540bbba5bf380d9285ed553443e834249352`.

`venice_sunset.nenv` is prepared by Nova's optional offline tool: 256-pixel GGX-prefiltered lighting faces (512 samples/texel), nine diffuse SH coefficients, and a separate 512-pixel display cube with a spatial mip chain prepared from the 4K HDR source. Radiance is stored in linear RGBA16F; sky and reflection lighting share the same orientation. Runtime consumers do not require the tool or HDR decoding dependencies.

Rebuild:

```sh
cmake -S . -B build -DNOVA_BUILD_TOOLS=ON
cmake --build build --target nova_environment_bake
# Download the official 4K background source separately (not bundled in the repository).
mkdir -p build/hdr-sources
curl -fL https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/4k/venice_sunset_4k.hdr -o build/hdr-sources/venice_sunset_4k.hdr
./build/nova_environment_bake assets/Environment/venice_sunset_1k.hdr assets/Environment/venice_sunset.nenv 256 512 build/hdr-sources/venice_sunset_4k.hdr 512
```

The internal `NOVENV02` format extends the original 160-byte header with two uint32 values: background face size and mip count. It then stores the original IBL mip-major six-face RGBA16F payload followed by the independent background mip chain. Both cubes use +X, -X, +Y, -Y, +Z, -Z order. The 168-byte header retains the original IBL dimensions and diffuse SH coefficients. `NOVENV01` remains readable, using its IBL cube as a background fallback.

The background uses four spatial samples per base texel and ordinary box-downsampled mip levels, not GGX roughness filtering. The shader selects a spatial mip from derivatives. The original IBL payload and SH coefficients are unchanged. Each current resource is approximately 20 MiB; rendering and lighting can be prepared at different resolutions without changing the public API.

## Additional environments

- [Studio Small 09](https://polyhaven.com/a/studio_small_09), Sergej Majboroda, CC0.
- [Kloppenheim 06](https://polyhaven.com/a/kloppenheim_06), Greg Zaal, CC0.

Original 1K HDR files are unmodified. Each `.nenv` uses the same offline preparation and format as Venice Sunset. Rebuild by replacing the file names in the command above.

SHA-256:

- `studio_small_09_1k.hdr`: `e7cfda5f4e98e623db12b8bfd0184e048488e4855d9c83e2751fb44a32e80c45`

- `studio_small_09.nenv`: `1e16736f21cdde5a75daf327c663d0c7147baf019ef790d697829e5da84e8bcd`

- `kloppenheim_06_1k.hdr`: `e3f27189b1e8fdeeaa7aa1f4dc4aad54a4c4294870dc89ad23479d563e3a0f70`

- `kloppenheim_06.nenv`: `cbf7f366095610ad3b204c9c77ee81da8d6cdec70b14bd840eb46b4f148c9dc3`

## 4K background source verification

Original 4K HDR files are downloaded from `https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/4k/<name>_4k.hdr`. Replace the asset name in the rebuild command above. Their authors and CC0 terms are the same as the 1K sources. Only prepared runtime cubes are committed; the 4K originals remain optional build inputs.

- `venice_sunset_4k.hdr`: `f243a060f84df1437705d2e50bbdbcce66aa55ddb2d913c18eb3f87873213b10`

- `studio_small_09_4k.hdr`: `71a876b4315e691cc095e7340228dc9bc48417c26a681b4fc30938161fc85e39`

- `kloppenheim_06_4k.hdr`: `0e668c7a13101343d8cff37cdab4b440bb3483a4c8f9e8435df53e12eb843150`

Current `venice_sunset.nenv` SHA-256: `9ca4eac3c0f0ce81757b7ce74bea29e7bf9d3f825588bddd866e27ee6dfa2ff2`.
