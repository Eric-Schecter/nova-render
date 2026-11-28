# Scene picking

Available on `main` for the next SDK version; not included in v0.1.0. Native and web use matching CPU asset-local BVHs and triangle-intersection semantics. Picking does not require hardware ray tracing or a GPU readback.

## API

C++: `Renderer::PickScene(const Ray&) const`, also forwarded by `Window`. Returns `std::optional<SceneHit>`.

JavaScript: `renderer.pickScene(ray)`. Returns a hit object or `null` synchronously.

```cpp
render::Ray ray;
ray.origin = {0, 0, 5};
ray.direction = {0, 0, -1};
ray.includeIds = {"front", "back"};
ray.excludeIds = {"front"};
const auto hit = window.PickScene(ray);
if (hit) {
    // hit->id, hit->position, hit->normal, hit->distance
}
```

```js
const hit = renderer.pickScene({
  origin: [0, 0, 5],
  direction: [0, 0, -1],
  includeIds: ['front', 'back'],
  excludeIds: ['front'],
})
// hit: { id, position: [x,y,z], normal: [x,y,z], distance } | null
```

## Ray and filters

- `origin` and `direction` are world-space vectors of three finite float32 values. JavaScript accepts arrays or Float32Arrays. The direction must be nonzero; it is normalized internally. Its magnitude does not change the returned world-space distance.
- C++ `Ray` defaults to origin `[0,0,0]` and direction `[0,0,-1]`. JavaScript requires both vectors.
- `includeIds` and `excludeIds` contain **instance IDs**, not asset IDs. Both default to empty lists.
- Empty `includeIds` means all instances. Otherwise only listed instances are considered. Exclusions always take precedence, including when an ID appears in both lists.
- Unknown IDs are ignored. Duplicate IDs behave as one. Filter IDs must be nonempty strings; invalid rays or lists throw even when the scene is empty.

The query considers both sides of triangles and returns the nearest intersection at distance **greater than or equal to zero**. Starting on a surface may return distance zero; exclude that instance when avoiding self-picks. Equal-distance instances resolve in `SetScene` input order. At a shared triangle edge, equal-distance faces resolve by their original triangle order.

## Hit result

- `id`: a copy of the matching instance ID.
- `triangleIndex`: zero-based triangle index in the prepared asset stream. Its order is retained across pose updates with fixed topology.
- `barycentric`: weights for the three vertices in triangle order.
- `uv`: interpolated primary texture coordinate stored in the prepared geometry, before sampler wrapping.
- `position`: the world-space hit point.
- `normal`: a world-space unit **geometric** normal, computed from the transformed triangle winding. It is not interpolated, normal-mapped or automatically flipped toward the ray. A mirrored instance reverses winding and therefore reverses the normal. For a facing normal, negate it when its dot product with the incoming direction is positive.
- `distance`: world-space distance from the ray origin; C++ stores it as `double`. Positions and normals use float32 components. Both source and normalized asset spaces are mapped through the same instance transforms as rendering.

Results are copies and remain valid after scene updates. Filtering is read-only: it does not hide or modify rendered instances.

## Scene state and limits

Queries read the last successfully committed `SetScene` state; drawing a frame is not required first. In JavaScript, `await renderer.setScene(...)` before querying its new state. While an asynchronous update is pending, queries still read the previous scene. Failed updates preserve it. Empty scenes and legacy `LoadModel` / `loadModel` mode return no hit.

This API currently targets static opaque scene assets supported by `SetScene`. It skips singular instance transforms and zero-area triangles. It does not pick animated single-model geometry, gizmos or UI. CPU BVHs are shared per asset and reused for transform/coordinate-space updates; instances are filtered and traversed on the CPU. Very large scenes may need a future instance-level acceleration structure.

A ray query is independent of camera, viewport, depth buffer and camera clipping planes. The caller converts pointer coordinates to a world-space ray and applies any desired camera range. The demo below uses a fixed orthographic camera; its pointer conversion belongs to the example, not the picking API. The right instance is mirrored and rotated to demonstrate a non-axis-aligned geometric normal. Click a box or use the keyboard-accessible controls. Excluding the red front instance lets the same ray hit the blue instance behind it.

<ClientOnly><PickingPlayground /></ClientOnly>
