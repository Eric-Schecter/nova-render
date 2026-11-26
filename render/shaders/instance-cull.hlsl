struct InstanceRecord
{
    float4 world[4];
    float4 normal[4];
    float4 baseColor;
    float4 material;
    float4 emissive;
    float4 uv;
};
struct BatchBounds
{
    float4 center;
    float4 extent;
};
struct DrawCommand
{
    uint vertexCount, instanceCount, firstVertex, firstInstance;
};
struct CullParameters
{
    float4 planes[6];
    uint instanceCount, unused, dispatchWidth, pad;
};
[[vk::push_constant]] CullParameters params;
[[vk::binding(0, 0)]] RWStructuredBuffer<InstanceRecord> instances : register(u0);
[[vk::binding(1, 0)]] StructuredBuffer<uint> instanceBatches : register(t0);
[[vk::binding(2, 0)]] StructuredBuffer<BatchBounds> bounds : register(t1);
[[vk::binding(3, 0)]] RWStructuredBuffer<DrawCommand> commands : register(u1);

[numthreads(64, 1, 1)]
void CullInstances(uint3 id : SV_DispatchThreadID)
{
    uint index = id.x + id.y * params.dispatchWidth;
    if (index >= params.instanceCount)
        return;
    uint batch = instanceBatches[index];
    BatchBounds box = bounds[batch];
    InstanceRecord instance = instances[index];
    if (box.extent.w > 0)
    {
        float3 a = instance.world[0].xyz;
        float3 b = instance.world[1].xyz;
        float3 c = instance.world[2].xyz;
        float3 center = instance.world[3].xyz + a * box.center.x + b * box.center.y + c * box.center.z;
        [unroll]
        for (uint i = 0; i < 6; ++i)
        {
            float4 plane = params.planes[i];
            float radius = dot(box.extent.xyz, abs(float3(dot(plane.xyz, a), dot(plane.xyz, b), dot(plane.xyz, c))));
            float distance = dot(plane.xyz, center) + plane.w;
            // Round outward near the frustum, including at large world coordinates.
            float padding = 1e-5 * (1 + abs(plane.w) + dot(abs(plane.xyz), abs(center)) + radius);
            if (distance < -radius - padding)
                return;
        }
    }
    uint slot;
    InterlockedAdd(commands[batch].instanceCount, 1, slot);
    instances[commands[batch].firstInstance + slot] = instance;
}
