// Composite weighted blended OIT over opaque/masked color, before post-processing.
cbuffer Parameters : register(b0)
{
    float4 aa[16];
}
Texture2D<float4> opaqueColor : register(t1);
Texture2D<float4> accumulation : register(t2);
Texture2D<float4> revealage : register(t3);
float4            VSMain(uint id : SV_VertexID)
  : SV_Position
{
    return float4(float2((id << 1) & 2, id & 2) * 2 - 1, 0, 1);
}
float4 PSMain(float4 position : SV_Position)
  : SV_Target
{
    int3   pixel = int3(int2(position.xy), 0);
    float3 base  = opaqueColor.Load(pixel).rgb;
    float4 sum   = accumulation.Load(pixel);
    float  alpha = 1 - saturate(revealage.Load(pixel).r);
    return float4(lerp(base, sum.rgb / max(sum.a, 1e-6), alpha), 1);
}
