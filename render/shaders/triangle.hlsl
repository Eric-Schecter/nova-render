struct VertexOut
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};
VertexOut VSMain(uint id : SV_VertexID)
{
    const float2 positions[3] = {float2(0.0, 0.65), float2(0.65, -0.55), float2(-0.65, -0.55)};
    const float3 colors[3]    = {float3(1.0, 0.25, 0.2), float3(0.2, 1.0, 0.4), float3(0.2, 0.45, 1.0)};
    VertexOut    output;
    output.position = float4(positions[id], 0.0, 1.0);
    output.color    = colors[id];
    return output;
}
#if defined(NOVA_VULKAN_PRESENT)
[[vk::constant_id(0)]] const uint encodeVulkanOutput = 0;
float encodeDisplayChannel(float value)
{
    return value <= .0031308 ? 12.92 * value : 1.055 * pow(value, 1 / 2.4) - .055;
}
#endif
float4 PSMain(VertexOut input)
  : SV_Target
{
    float3 color = input.color;
#if defined(NOVA_VULKAN_PRESENT)
    if (encodeVulkanOutput)
        color = float3(encodeDisplayChannel(color.r), encodeDisplayChannel(color.g), encodeDisplayChannel(color.b));
#endif
    return float4(color, 1.0);
}
