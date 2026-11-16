# Skeletal animation

Window, Renderer and the web adapter expose matching clip and playback controls after loading a GLB containing a skin and animation.

## C++

```cpp
auto clips = window.GetAnimations();
auto state = window.GetAnimationState();
state.clip = 1;
state.time = 0;
state.speed = 1;
state.playing = true;
state.loop = true;
window.SetAnimationState(state);


window.UpdateAnimation(deltaSeconds);
window.DrawFrame();
```

## Web

```js
const clips = renderer.getAnimations()
renderer.setAnimationState({
  clip: 1,
  time: 0,
  speed: 1,
  playing: true,
  loop: true,
})
await renderer.drawFrame()
```

The web renderer advances playback after the first draw and pauses when hidden or in a background tab, without catching up hidden time. Native applications call `UpdateAnimation(deltaSeconds)`. The browser also exposes `updateAnimation(deltaSeconds)`; for manual inspection pause playback and set `time` directly rather than advancing twice.

- `GetAnimations()` / `getAnimations()`: clip list, empty for static models; each has name and duration in seconds.
- `GetAnimationState()` / `getAnimationState()`: state copy.
- `SetAnimationState` / `setAnimationState`: clip index (zero-based), time [0,duration], speed [0.1,4], playing and loop. Invalid values throw. Reset time to zero when changing clips.
- `UpdateAnimation` / `updateAnimation`: finite nonnegative delta seconds; paused clips do not advance, and non-looping clips stop at the end.

## Scope

Follows [glTF 2.0 skins](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#skins): joint hierarchy, inverse bind matrices and up to four weights per vertex. Translation/rotation/scale channels support LINEAR and STEP; rotations use quaternion interpolation. Weights may be float or normalized uint8/uint16, with uint8/uint16 joint indices.

CPU code samples keyframes and computes matrices; shared HLSL vertex code skins positions and normals on the GPU. Bind-pose vertices and weights upload once; only palettes and scene constants change per frame. Inverse-transpose normal handling supports nonuniform scaling. Native and WebGPU use the same HLSL, without new dependencies or WASM.

Opaque single-material embedded GLB only: no morphs, CUBICSPLINE, animation blending or compressed extensions; transforms must be invertible. Animated models currently disable ray tracing and report the reason via capabilities. Static ray tracing and denoising remain available.

## Validation and example

CPU reference skinning is test-only; see `tools/gpu_skinning/README.md` for Metal readback and browser comparisons. The robot regression reduces per-frame deformation uploads from about 25.3 KiB of vertices to 1.5 KiB of matrices; Fox uses 3.125 KiB.

Example 5 uses the original Fox GLB: 24 joints and Survey / Walk / Run clips. Robot assets remain regression fixtures. Environment controls match example 4: three HDR maps, rotation, lighting/sky intensity and visibility. Default illumination is environment-only; switching maps preserves playback state.

```sh
./build/nova_animation assets/Fox/Fox.glb --api metal
./build/nova_animation assets/Fox/Fox.glb --environment assets/Environment/studio_small_09.nenv
```
