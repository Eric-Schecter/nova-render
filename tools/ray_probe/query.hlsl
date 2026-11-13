[[vk::binding(0, 0)]] RaytracingAccelerationStructure scene : register(t0);
[[vk::binding(1, 0)]] RWStructuredBuffer<uint4>       results : register(u0);
[numthreads(4, 1, 1)] void                            CSMain(uint3 id : SV_DispatchThreadID) {
    RayDesc ray;
    ray.Origin    = float3(id.x == 1 ? 4 : 0, 0, id.x == 2 ? -1 : 1);
    ray.Direction = float3(0, 0, id.x == 2 ? 1 : -1);
    ray.TMin      = id.x == 3 ? 2 : 0.001;
    ray.TMax      = 100;
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(scene, RAY_FLAG_NONE, 255, ray);
    while (query.Proceed())
    {
    }
    const bool   hit  = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    const float2 bary = hit ? query.CommittedTriangleBarycentrics() : float2(0, 0);
    results[id.x * 2] =
      uint4(hit ? 1 : 0, hit ? asuint(query.CommittedRayT()) : 0, hit ? query.CommittedPrimitiveIndex() : 0, 0);
    results[id.x * 2 + 1] = uint4(
      asuint(bary.x),
      asuint(bary.y),
      hit ? query.CommittedInstanceIndex() : 0,
      hit ? uint(query.CommittedTriangleFrontFace()) : 0);
}
