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

## Additional environments

- [Studio Small 09](https://polyhaven.com/a/studio_small_09), Sergej Majboroda, CC0.
- [Kloppenheim 06](https://polyhaven.com/a/kloppenheim_06), Greg Zaal, CC0.

Original 1K HDR files are unmodified. Each `.nenv` uses the same offline preparation and format as Venice Sunset. Rebuild by replacing the file names in the command above.

SHA-256:

- `studio_small_09_1k.hdr`: `e7cfda5f4e98e623db12b8bfd0184e048488e4855d9c83e2751fb44a32e80c45`

- `studio_small_09.nenv`: `fb5a304a8e051d9008de389d055c74cdd1256a2914e7a3e91f3debb881316b12`

- `kloppenheim_06_1k.hdr`: `e3f27189b1e8fdeeaa7aa1f4dc4aad54a4c4294870dc89ad23479d563e3a0f70`

- `kloppenheim_06.nenv`: `557e314f0a73eeb8c1c022b7e8b38e48125e41fe68921bc14e986ed4c53ebfbd`
