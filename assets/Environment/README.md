# Venice Sunset environment

Source: [Venice Sunset](https://polyhaven.com/a/venice_sunset) by Greg Zaal, Poly Haven.
License: [CC0](https://polyhaven.com/license).

`venice_sunset_1k.hdr` is the original Radiance HDR file. SHA-256:
`92fe2b29b1957828fb8925a57633540bbba5bf380d9285ed553443e834249352`.

`venice_sunset.nenv` is prepared by Nova's optional offline tool: 256-pixel cube faces, a full GGX-prefiltered mip chain (512 samples/texel), and nine diffuse SH coefficients. Radiance is stored in linear RGBA16F; sky and reflection lighting share the same orientation. Runtime consumers do not require the tool or HDR decoding dependencies.

Rebuild:

```sh
cmake -S . -B build -DNOVA_BUILD_TOOLS=ON
cmake --build build --target nova_environment_bake
./build/nova_environment_bake assets/Environment/venice_sunset_1k.hdr assets/Environment/venice_sunset.nenv
```

The versioned binary format is internal: `NOVENV01`, little-endian face size and level count, 9 × float4 convolved diffuse SH coefficients (irradiance / pi), followed by mip-major cube faces in +X, -X, +Y, -Y, +Z, -Z order, tightly packed RGBA16F. It is validated on load. This is an offline GGX approximation, not a general HDR/IBL interchange format.
