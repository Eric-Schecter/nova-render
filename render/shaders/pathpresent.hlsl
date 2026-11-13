[[vk::binding(0, 0)]] StructuredBuffer<uint>   pixels : register(t0);
[[vk::binding(1, 0)]] StructuredBuffer<float4> parameters : register(t1);
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
float4 pixel(int2 p, int2 size)
{
    p      = clamp(p, int2(0, 0), size - 1);
    uint c = pixels[p.y * size.x + p.x];
    return float4(c & 255, (c >> 8) & 255, (c >> 16) & 255, 255) / 255.0;
}
float4 PSMain(Output input)
  : SV_Target0
{
    int2   size = int2(asuint(parameters[24]).yz);
    float2 p    = input.uv * float2(size) - .5;
    int2   b    = int2(floor(p));
    float2 f    = frac(p);
    return lerp(
      lerp(pixel(b, size), pixel(b + int2(1, 0), size), f.x),
      lerp(pixel(b + int2(0, 1), size), pixel(b + 1, size), f.x),
      f.y);
}
