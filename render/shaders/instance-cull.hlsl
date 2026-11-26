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
#ifdef NOVA_D3D12_INSTANCE_CULL
    uint firstVertex, firstInstance, vertexCount, instanceCount, startVertex, startInstance;
#else
    uint vertexCount, instanceCount, firstVertex, firstInstance;
#endif
};
struct CullParameters
{
    float4 planes[6];
    uint instanceCount, unused, dispatchWidth, pad;
};
#if defined(NOVA_D3D12_INSTANCE_CULL)
ConstantBuffer<CullParameters> params : register(b0);
#elif defined(NOVA_METAL_INSTANCE_CULL)
[[vk::binding(4, 0)]] ConstantBuffer<CullParameters> params : register(b0);
#else
[[vk::push_constant]] CullParameters params;
#endif
#ifdef NOVA_D3D12_INSTANCE_CULL
#define CULL_BINDING(slot)
#else
#define CULL_BINDING(slot) [[vk::binding(slot, 0)]]
#endif
#ifdef NOVA_VISIBLE_INSTANCE_INDICES
CULL_BINDING(0) StructuredBuffer<InstanceRecord> instances : register(t2);
CULL_BINDING(4) RWStructuredBuffer<uint> visibleInstances : register(u2);
#else
CULL_BINDING(0) RWStructuredBuffer<InstanceRecord> instances : register(u0);
#endif
CULL_BINDING(1) StructuredBuffer<uint> instanceBatches : register(t0);
CULL_BINDING(2) StructuredBuffer<BatchBounds> bounds : register(t1);
CULL_BINDING(3) RWStructuredBuffer<DrawCommand> commands : register(u1);

[numthreads(64, 1, 1)]
void CullInstances(uint3 id : SV_DispatchThreadID)
{
    uint index = id.x + id.y * params.dispatchWidth;
    if (index >= params.instanceCount)
        return;
    uint batch = instanceBatches[index];
    BatchBounds box = bounds[batch];
#ifndef NOVA_VISIBLE_INSTANCE_INDICES
    InstanceRecord instance = instances[index];
#endif
    if (box.extent.w > 0)
    {
#ifdef NOVA_VISIBLE_INSTANCE_INDICES
        float3 a = instances[index].world[0].xyz;
        float3 b = instances[index].world[1].xyz;
        float3 c = instances[index].world[2].xyz;
        float3 center = instances[index].world[3].xyz + a * box.center.x + b * box.center.y + c * box.center.z;
#else
        float3 a = instance.world[0].xyz;
        float3 b = instance.world[1].xyz;
        float3 c = instance.world[2].xyz;
        float3 center = instance.world[3].xyz + a * box.center.x + b * box.center.y + c * box.center.z;
#endif
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
#ifdef NOVA_VISIBLE_INSTANCE_INDICES
    visibleInstances[commands[batch].firstInstance + slot] = index;
#else
    instances[commands[batch].firstInstance + slot] = instance;
#endif
}
