# CarConcept showroom

CarConcept model and textures by Eric Chadwick, © 2024 Darmstadt Graphics Group GmbH, [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Adapted from [Khronos glTF Sample Assets](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/CarConcept). Embedded Khronos and 3D Commerce logos are Khronos marks and retain their [upstream terms](https://github.com/KhronosGroup/glTF-Sample-Assets/blob/main/LICENSES/LicenseRef-LegalMark-Khronos.txt).

The Nova adaptation selects the Carmine paint, bakes UV transforms, omits other paint variants and iridescence, approximates extended emissive strength, and retains original glass transmission. Ground.glb is the neutral showroom floor. CleanStudio.nenv is a generated neutral HDR environment with smooth lights and no architectural features. The same baked environment is used by the native and browser examples.

Regenerate the environment after building `nova_environment_bake` with `NOVA_BUILD_TOOLS=ON`:

```sh
python3 scripts/generate-clean-studio.py build/clean-studio.hdr
./build/nova_environment_bake build/clean-studio.hdr assets/CarConcept/CleanStudio.nenv 128 256
```

The showroom tunes Paint 1 Carmine's base roughness from 0.25 to 0.42. Its clearcoat and all other CarConcept material fields retain the imported values. The floor has a baked contact mask under the four tires, aligned to the lowest tread vertices in this parked pose. The showroom seats all four treads on the floor, with up to 1.5 mm of tread overlap and about 87 mm of minimum body clearance. Regenerate it with `python3 scripts/generate-showroom-ground.py` from the repository root.
