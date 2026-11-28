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

Follows [glTF 2.0 skins](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#skins): joint hierarchy, inverse bind matrices and up to four weights per vertex. Translation/rotation/scale channels support LINEAR, STEP and CUBICSPLINE. Cubic quaternion results are normalized after component-wise interpolation. Weights may be float or normalized uint8/uint16, with uint8/uint16 joint indices.

CPU code samples keyframes and computes matrices; shared HLSL vertex code skins positions and normals on the GPU. Bind-pose vertices and weights upload once; only palettes and scene constants change per frame. Inverse-transpose normal handling supports nonuniform scaling. Native and WebGPU use the same HLSL, without new dependencies or WASM.

Opaque embedded GLB with runtime-editable material slots: no morphs, CUBICSPLINE, animation blending or compressed extensions; transforms must be invertible. Animated models currently disable ray tracing and report the reason via capabilities. Static ray tracing and denoising remain available.

## Validation and example

Raster deformation runs on the GPU. Pose queries additionally reconstruct and cache CPU geometry on demand; see `tools/gpu_skinning/README.md` for Metal readback and browser comparisons. The robot regression reduces per-frame deformation uploads from about 25.3 KiB of vertices to 1.5 KiB of matrices; Fox uses 3.125 KiB.

Example 5 uses the original Fox GLB: 24 joints and Survey / Walk / Run clips. Robot assets remain regression fixtures. Environment controls match example 4: three HDR maps, rotation, lighting/sky intensity and visibility. The default HDR environment and directional light show self-shadows that follow the Fox pose. Switching maps preserves playback state.

```sh
./build/nova_animation assets/Fox/Fox.glb --api metal
./build/nova_animation assets/Fox/Fox.glb --environment assets/Environment/studio_small_09.nenv
```

## Scene instances (0.9.0)

`SetScene` / `setScene` accepts skeletal and rigid-node TRS animation assets. Geometry, skin inputs and textures remain shared per asset; every instance has an independent matrix palette and starts paused at clip 0, time 0. Scene animation uses an explicit application clock on both platforms; DrawFrame and browser RAF never advance it.

```cpp
auto clips = window.GetAnimations("character");
auto state = window.GetAnimationState("hero");
state.clip = 1;
state.time = 0;
state.playing = true;
window.SetAnimationState("hero", state);
window.UpdateAnimations(deltaSeconds);
window.DrawFrame();
```

```js
const clips = renderer.getAnimations('character')
renderer.setAnimationState('hero', { clip: 1, time: 0, playing: true })
renderer.updateAnimations(deltaSeconds)
await renderer.drawFrame()
```

The asset ID names a referenced scene asset; the instance ID names one placed object. Unknown IDs throw; static assets return an empty clip list. Web setters merge partial state; C++ receives the complete state struct. Reset time explicitly when changing clips. Time is [0, duration], speed is [0.1, 4], and delta is finite/nonnegative. A paused seek to duration stays on the last frame, even with loop enabled. Non-looping playback stops at duration. Invalid changes preserve all instance states and palettes.

SetScene preserves state for unchanged instance ID + asset ID + asset path/URL, including transform edits and material changes. Removed IDs are invalidated; reintroduced IDs start paused at zero. Filtering a hidden object out of SetScene counts as removal; the editor can retain its preview state separately. Replacing an asset path resets affected instances. Existing single-model methods remain compatible.

`GetSceneInstanceBounds(id)` / `getSceneInstanceBounds(id)` returns the current world-space pose AABB, or no value for an absent instance. Animated `GetSceneAssetBounds` / `getSceneAssetBounds` conservatively unions the active poses in the selected asset space. PickScene uses the current pose and the existing include/exclude filters; it remains geometric rather than alpha-aware.

`GetAnimationCapabilities()` / `getAnimationCapabilities()` reports skeletal, node TRS, GPU skinning, manual clock and posed picking/bounds. TRS animation works with LINEAR, STEP and CUBICSPLINE. Morph animation remains unsupported. Scene animation is Raster-only: Hybrid/ray modes are rejected atomically. Since 0.9.1, animated instances loaded with SetScene cast and receive scene shadows at their current pose (`dynamicShadows: true` in Raster). Set the instance castShadow flag to disable casting. Changed poses rebuild their CPU shadow BVHs and use the same HLSL traversal as static casters. Since 0.9.2, legacy single-model playback through LoadModel also casts and receives raster self-shadows when SetShadows enables them, or when an active scene light casts shadows.

## Read the current joint poses

After setting or advancing a scene instance's animation, call
`GetSceneJointPoses(instanceId)` in C++ or `getSceneJointPoses(instanceId)` on Web.
The synchronous result is a snapshot of the palette used for the current pose.
Each entry contains `nodeIndex` (the glTF joint node index), `name`, and
`world` (a column-major joint-to-world matrix). The matrix includes the
instance transform and omits the inverse bind matrix. Translation is stored
at indices 12 to 14.

Joints shared by skins appear once, ordered by node index. Names can be empty
or repeated. Node-only animations return an empty list and an unknown or
unanimated instance raises an error. Call the API after sampling and before
advancing again when driving physics attachments for the same tick.

## Share a skeleton across scene instances

Set `SceneInstance::skinSource` in C++ or `skinSource` on a Web scene instance
to the ID of the character instance. The wearable must be skinned. Its joint names
must match unique joints on the character, and its inverse bind matrices must
be authored for the corresponding rest pose.

Sample or advance the character animation as usual. The wearable uses the
character's current world-space joint poses and its own skin weights and inverse
bind matrices. Replacing a wearable through the scene API preserves the animation
time when the character instance and asset stay the same.

```js
await renderer.setScene(assets, [
  { id: 'actor', assetId: 'character' },
  { id: 'jacket', assetId: 'jacketModel', skinSource: 'actor' },
])
```

A source must have its own skeleton and cannot itself bind to another instance.
Omit the field (or use an empty string) to animate the instance independently.
