#include "texture-filtering.hlsli"
// Nova AA laboratory. Scene geometry and all reconstruction passes are shared by native and web.
cbuffer AAParameters : register(b0)
{
    float4 aa[8];
}
Texture2D<float4> colorTex : register(t1);
Texture2D<float4> edgeTex : register(t2);
Texture2D<float4> weightTex : register(t3);
Texture2D<float4> historyTex : register(t4);
Texture2D<float4> motionTex : register(t5);
Texture2D<float4> oldMotionTex : register(t6);
Texture2D<float4> areaTex : register(t7);
Texture2D<float4> searchTex : register(t8);
SamplerState      LinearSampler : register(s9);
SamplerState      PointSampler : register(s10);
#ifndef AA_PASS
#define AA_PASS 0
#endif
float3 gammaRGB(float3 c)
{
    return pow(saturate(c), 1.0 / 2.2);
}
float3 decodeSRGB(float3 c)
{
    return float3(
      c.x <= .04045 ? c.x / 12.92 : pow((c.x + .055) / 1.055, 2.4),
      c.y <= .04045 ? c.y / 12.92 : pow((c.y + .055) / 1.055, 2.4),
      c.z <= .04045 ? c.z / 12.92 : pow((c.z + .055) / 1.055, 2.4));
}
float3 encodeSRGB(float3 c)
{
    c = max(c, 0);
    return float3(
      c.x <= .0031308 ? c.x * 12.92 : 1.055 * pow(c.x, 1 / 2.4) - .055,
      c.y <= .0031308 ? c.y * 12.92 : 1.055 * pow(c.y, 1 / 2.4) - .055,
      c.z <= .0031308 ? c.z * 12.92 : 1.055 * pow(c.z, 1 / 2.4) - .055);
}
float4 rgbL(float3 c)
{
    return float4(c, dot(gammaRGB(c), float3(.299, .587, .114)));
}
struct Varying
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
#if AA_PASS == 0
    float3                color : TEXCOORD1;
    float4                current : TEXCOORD2;
    float4                previous : TEXCOORD3;
    nointerpolation float material : TEXCOORD4;
#endif
};
#if AA_PASS == 0
float2 rotate2(float2 p, float angle)
{
    float s = sin(angle), c = cos(angle);
    return float2(c * p.x - s * p.y, s * p.x + c * p.y);
}
float3 quadPoint(float3 o, float3 x, float3 y, uint v)
{
    uint corners[6] = {0, 1, 2, 2, 1, 3};
    uint c          = corners[v];
    return o + x * float(c & 1) + y * float((c >> 1) & 1);
}
float4 projectScene(float3 p, float yaw, float distance, float pitch)
{
    p.xz = rotate2(p.xz, yaw);
    p.yz = rotate2(p.yz, -pitch);
    p.z += distance;
    return float4(p.x * 1.85 / (aa[0].x / aa[0].y), p.y * 1.85, p.z * .99 - .1, p.z);
}
Varying VSMain(uint vertex : SV_VertexID)
{
    Varying o;
    uint    q = vertex / 6, v = vertex % 6;
    float3  p, prev;
    float   material = 0;
    float3  c        = float3(.12, .15, .20);
    float2  tex      = float2(0, 0);
    if (q == 0)
    {
        p        = quadPoint(float3(-4, -1, -4), float3(8, 0, 0), float3(0, 0, 8), v);
        tex      = p.xz * 12;
        material = 1;
    }
    else if (q == 1)
    {
        p = quadPoint(float3(-3, -1, 2), float3(6, 0, 0), float3(0, 3.3, 0), v);
        c = float3(.025, .035, .055);
    }
    else if (q < 43)
    {
        float x  = -2.8 + float(q - 2) * .135;
        p        = quadPoint(float3(x, -1, .8), float3(.014, 0, 0), float3(0, 2.35, 0), v);
        c        = float3(.8, .85, .9);
        material = 2;
    }
    else if (q < 46)
    {
        p        = quadPoint(float3(-2.8, -.65 + float(q - 43) * .8, .79), float3(5.55, 0, 0), float3(0, .025, 0), v);
        c        = float3(.6, .67, .75);
        material = 2;
    }
    else if (q == 46)
    {
        p        = quadPoint(float3(-2, -.6, -.3), float3(1.8, 1.5, 0), float3(-.025, .03, 0), v);
        c        = float3(1, .35, .035);
        material = 3;
    }
    else
    {
        float a = float(q - 47) * 6.2831853 / 32;
        p       = quadPoint(float3(0, -.012, 0), float3(.85, 0, 0), float3(0, .024, 0), v);
        p.xy    = rotate2(p.xy, a + aa[0].z * .35);
        p += float3(.7 + sin(aa[0].z * .7) * .45, .05, -.65);
        c        = float3(.04, .65, .95);
        material = 4;
    }
    prev = p;
    if (material == 4)
    {
        float2 local = p.xy - float2(.7 + sin(aa[0].z * .7) * .45, .05);
        local        = rotate2(local, (aa[2].w - aa[0].z) * .35);
        prev.xy      = local + float2(.7 + sin(aa[2].w * .7) * .45, .05);
    }
    o.current  = projectScene(p, aa[2].x, aa[4].x, aa[4].z);
    o.previous = projectScene(prev, aa[2].y, aa[4].y, aa[4].w);
    o.position = o.current;
    o.position.xy += aa[1].xy * 2 / aa[0].xy * float2(1, -1) * o.position.w;
    o.color    = c;
    o.material = material;
    o.uv       = tex;
    return o;
}
struct SceneOutput
{
    float4 color : SV_Target0;
    float4 motion : SV_Target1;
};
SceneOutput PSMain(Varying i)
{
    SceneOutput o;
    float3      c                = i.color;
    float2      checkerFootprint = fwidth(i.uv);
    if (i.material == 1)
    {
        float checker =
          aa[6].y > .5 ? filteredChecker(i.uv, checkerFootprint) : fmod(abs(floor(i.uv.x) + floor(i.uv.y)), 2);
        c = lerp(float3(.015, .022, .03), float3(.8, .82, .85), checker);
    }
    o.color     = rgbL(c * aa[2].z);
    float2 cur  = i.current.xy / i.current.w * float2(.5, -.5) + .5,
           prev = i.previous.xy / i.previous.w * float2(.5, -.5) + .5;
    o.motion    = float4(prev - cur, i.current.w / 16, i.material / 8);
    return o;
}
#else
Varying VSMain(uint vertex : SV_VertexID)
{
    Varying o;
    float2  p  = float2((vertex << 1) & 2, vertex & 2);
    o.uv       = p;
    o.position = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
#if AA_PASS == 2
#define FXAA_PC 1
#define FXAA_HLSL_4 1
#define FXAA_GREEN_AS_LUMA 0
#define FXAA_QUALITY__PRESET 29
#include "third_party/fxaa/FXAA.hlsli"
#endif
#if AA_PASS >= 4 && AA_PASS <= 6
#define SMAA_HLSL_4 1
#define SMAA_PRESET_HIGH 1
#define SMAA_RT_METRICS float4(1 / aa[0].xy, aa[0].xy)
#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 1
#include "third_party/smaa/SMAA.hlsl"
#endif
float4 PSMain(Varying i)
  : SV_Target0
{
    float2 uv = i.uv, texel = 1 / aa[0].xy;
#if AA_PASS == 1
    // Linear downsample: SSAA resolves every 2x2 shading sample, not just coverage.
    float4 value = colorTex.SampleLevel(LinearSampler, uv * aa[5].xy + aa[5].zw, 0);
    if (aa[6].z > 1.5)
        value.rgb = decodeSRGB(value.rgb);
    return aa[6].x > .5 ? rgbL(value.rgb) : value;
#elif AA_PASS == 2
    FxaaTex t = {LinearSampler, colorTex};
    return FxaaPixelShader(uv, 0, t, t, t, texel, 0, 0, 0, .75, .166, .0833, 0, 0, 0, 0);
#elif AA_PASS == 3
    return float4(gammaRGB(colorTex.SampleLevel(LinearSampler, uv, 0).rgb), 1);
#elif AA_PASS == 4
    float4 offset[3];
    SMAAEdgeDetectionVS(uv, offset);
    return float4(SMAALumaEdgeDetectionPS(uv, offset, colorTex), 0, 0);
#elif AA_PASS == 5
    float4 offset[3];
    float2 pixel;
    SMAABlendingWeightCalculationVS(uv, pixel, offset);
    return SMAABlendingWeightCalculationPS(uv, pixel, offset, edgeTex, areaTex, searchTex, 0);
#elif AA_PASS == 6
    float4 offset;
    SMAANeighborhoodBlendingVS(uv, offset);
    return SMAANeighborhoodBlendingPS(uv, offset, colorTex, weightTex);
#elif AA_PASS == 7
    float4 current = colorTex.SampleLevel(PointSampler, uv, 0), m = motionTex.SampleLevel(PointSampler, uv, 0);
    float2 prevUV = uv + m.xy + (aa[1].zw - aa[1].xy) / aa[0].xy;
    if (aa[3].x < .5 || any(prevUV < 0) || any(prevUV > 1))
        return current;
    float4 old = oldMotionTex.SampleLevel(PointSampler, prevUV, 0);
    if (abs(m.z - old.z) > .012 || abs(m.w - old.w) > .03)
        return current;
    float3 lo = current.rgb, hi = current.rgb;
    [unroll] for (int y = -1; y <= 1; y++)[unroll] for (int x = -1; x <= 1; x++)
    {
        float3 c = colorTex.SampleLevel(PointSampler, uv + float2(x, y) * texel, 0).rgb;
        lo       = min(lo, c);
        hi       = max(hi, c);
    }
    float3 history = clamp(historyTex.SampleLevel(LinearSampler, prevUV, 0).rgb, lo, hi);
    return rgbL(lerp(current.rgb, history, aa[3].y));
#elif AA_PASS == 8
    uv       = .5 + (uv - .5) / aa[3].z;
    float3 c = colorTex.SampleLevel(PointSampler, uv, 0).rgb;
    if (aa[3].w > .5 && i.uv.x < .5)
        c = historyTex.SampleLevel(PointSampler, uv, 0).rgb;
    if (aa[3].w > .5 && abs(i.uv.x - .5) < texel.x)
        c = float3(.2, .5, .8);
    return float4(aa[6].z > .5 ? encodeSRGB(c) : gammaRGB(c), 1);
#endif
}
#endif
