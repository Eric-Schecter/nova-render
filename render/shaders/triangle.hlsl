struct VertexOut { float4 position : SV_Position; float3 color : COLOR0; };
VertexOut VSMain(uint id : SV_VertexID) {
    const float2 positions[3] = {float2(0.0, 0.65), float2(0.65, -0.55), float2(-0.65, -0.55)};
    const float3 colors[3] = {float3(1.0, 0.25, 0.2), float3(0.2, 1.0, 0.4), float3(0.2, 0.45, 1.0)};
    VertexOut output;
    output.position = float4(positions[id], 0.0, 1.0);
    output.color = colors[id];
    return output;
}
float4 PSMain(VertexOut input) : SV_Target { return float4(input.color, 1.0); }
