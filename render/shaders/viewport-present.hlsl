[[vk::binding(0, 0)]] Texture2D<float4> image : register(t0);
[[vk::binding(1, 0)]] SamplerState      imageSampler : register(s0);
struct Output
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
Output VSMain(uint id : SV_VertexID)
{
    float2 p = float2((id << 1) & 2, id & 2);
    Output o;
    o.position = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv       = p;
    return o;
}
float4 PSMain(Output input)
  : SV_Target
{
    return image.SampleLevel(imageSampler, input.uv, 0);
}
