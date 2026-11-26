// Curve reference: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
// Shared HDR post chain. ACES uses the Narkowicz fitted curve, not a full ACES color pipeline.
cbuffer Parameters : register(b0)
{
    float4 aa[16];
}
Texture2D<float4> colorTex : register(t1);
Texture2D<float4> bloomTex : register(t2);
Texture2D<float4> exposureTex : register(t3);
SamplerState      LinearSampler : register(s9);
SamplerState      PointSampler : register(s10);
#ifndef AA_PASS
#define AA_PASS 16
#endif
float luminance(float3 c)
{
    return dot(max(c, 0), float3(.2126, .7152, .0722));
}
float3 gammaRGB(float3 c)
{
    return pow(saturate(c), 1 / 2.2);
}
float4 rgbL(float3 c)
{
    return float4(c, dot(gammaRGB(c), float3(.299, .587, .114)));
}
// ACES RRT + ODT fit used by three.js ACESFilmicToneMapping.
float3 acesFilmicToneMap(float3 c)
{
    const float3x3 inputMatrix = float3x3(
        .59719, .35458, .04823, .07600, .90834, .01566, .02840, .13383, .83777);
    const float3x3 outputMatrix = float3x3(
        1.60475, -.53108, -.07367, -.10208, 1.10813, -.00605, -.00327, -.07276, 1.07602);
    c = mul(inputMatrix, c / .6);
    float3 fit = (c * (c + .0245786) - .000090537) / (c * (.983729 * c + .4329510) + .238081);
    return saturate(mul(outputMatrix, fit));
}
// AgX default-look approximation in Rec.2020, following the three.js/Filament
// transform and Benjamin Wrensch's contrast fit. Returns linear Rec.709 for presentation.
float3 agxToneMap(float3 c)
{
    const float3x3 toWide = float3x3(
        .6274, .3293, .0433, .0691, .9195, .0113, .0164, .0880, .8956);
    const float3x3 inset = float3x3(
        .85662715, .09512124, .04825161,
        .13731897, .76124199, .10143904,
        .11189821, .07679942, .81130237);
    const float3x3 outset = float3x3(
        1.12710058, -.11060664, -.01649394,
        -.14132976, 1.15782370, -.01649394,
        -.14132976, -.11060664, 1.25193641);
    const float3x3 toDisplay = float3x3(
        1.6605, -.5876, -.0728, -.1246, 1.1329, -.0083, -.0182, -.1006, 1.1187);
    float3 x = saturate((log2(max(mul(inset, mul(toWide, c)), 1e-10)) + 12.47393) / 16.5);
    float3 film = (((((15.5 * x - 40.14) * x + 31.96) * x - 6.868) * x + .4298) * x + .1191) * x - .00232;
    return saturate(mul(toDisplay, pow(max(mul(outset, film), 0), 2.2)));
}
#if AA_PASS == 16
struct Geometry
{
    float3 p;
    float3 n;
    float3 color;
    float3 emission;
    float  roughness;
};
float3 lampColor(uint i)
{
    if (i == 0)
        return float3(1, .12, .03);
    if (i == 1)
        return float3(1, .5, .08);
    if (i == 2)
        return float3(.2, 1, .35);
    if (i == 3)
        return float3(.05, .5, 1);
    if (i == 4)
        return float3(.4, .12, 1);
    return float3(1, .7, .45);
}
Geometry geometry(uint id, float time)
{
    Geometry g;
    g.emission        = 0;
    g.roughness       = .6;
    g.color           = float3(.38, .4, .43);
    const uint tri[6] = {0, 1, 3, 0, 3, 2};
    if (id < 6)
    {
        uint q = tri[id];
        g.p    = float3((q & 1) ? 3.3 : -3.3, -1, (q & 2) ? 6.0 : -3.3);
        g.n    = float3(0, 1, 0);
        return g;
    }
    id -= 6;
    if (id < 19 * 36)
    {
        uint   object = id / 36, face = (id % 36) / 6, q = tri[id % 6];
        float2 uv = float2(q & 1, (q >> 1) & 1) * 2 - 1;
        float3 center, size;
        if (object == 0)
        {
            center = float3(0, .7, -2.7);
            size   = float3(6.6, 3.4, .1);
        }
        else if (object == 1)
        {
            center = float3(-3.3, .7, 0);
            size   = float3(.1, 3.4, 5.4);
        }
        else if (object == 2)
        {
            center = float3(3.3, .7, 0);
            size   = float3(.1, 3.4, 5.4);
        }
        else if (object == 3)
        {
            center = float3(0, 2.4, 0);
            size   = float3(6.6, .1, 5.4);
        }
        else if (object < 10)
        {
            uint light = object - 4;
            center     = float3(-2.5 + light, .5, -2.61);
            size       = float3(.035, 2.1, .05);
            g.emission = lampColor(light) * (12 + light * 5);
            g.color    = 0;
        }
        else
        {
            uint k      = object - 10;
            center      = float3(-2.4 + k * .6, -.8, .8);
            size        = float3(.42, .4, .42);
            g.roughness = .1 + k * .1;
            g.color     = float3(.6, .6, .6);
        }
        float sign = (face & 1) ? 1 : -1;
        uint  axis = face / 2;
        g.n        = axis == 0 ? float3(sign, 0, 0) : axis == 1 ? float3(0, sign, 0) : float3(0, 0, sign);
        float3 p   = axis == 0 ? float3(sign, uv) : axis == 1 ? float3(uv.x, sign, uv.y) : float3(uv, sign);
        g.p        = center + p * size * .5;
        return g;
    }
    id -= 19 * 36;
    uint  object = id / 2304, k = id % 2304, cell = k / 6, q = tri[k % 6];
    float u     = (float(cell % 24) + float(q & 1)) / 24 * 6.283185307,
          v     = (float(cell / 24) + float((q >> 1) & 1)) / 16 * 3.141592654;
    g.n         = float3(sin(v) * cos(u), cos(v), sin(v) * sin(u));
    g.p         = float3(-1.7 + object * 1.7, -.43, -.7 + sin(time) * .1) + g.n * .55;
    g.color     = object == 1 ? float3(.82, .48, .12) : float3(.5, .52, .55);
    g.roughness = .12 + object * .35;
    return g;
}
float3 cameraEye(float yaw, float pitch, float distance)
{
    return float3(sin(yaw) * cos(pitch), sin(pitch), cos(yaw) * cos(pitch)) * distance;
}
float4 clipPosition(float3 p, float yaw, float pitch, float distance)
{
    float  sy = sin(yaw), cy = cos(yaw), sp = sin(pitch), cp = cos(pitch);
    float3 v = float3(
      dot(p, float3(cy, 0, -sy)),
      dot(p, float3(-sy * sp, cp, -cy * sp)),
      distance - dot(p, float3(sy * cp, sp, cy * cp)));
    return float4(v.x * 1.85 / (aa[0].x / aa[0].y), v.y * 1.85, v.z * .99 - .1, v.z);
}
struct Varying
{
    float4 position : SV_Position;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 color : TEXCOORD2;
    float3 emission : TEXCOORD3;
    float  roughness : TEXCOORD4;
    float4 current : TEXCOORD5;
    float4 previous : TEXCOORD6;
};
Varying VSMain(uint id : SV_VertexID)
{
    Geometry g = geometry(id, aa[0].z);
    Varying  o;
    o.world     = g.p;
    o.normal    = g.n;
    o.color     = g.color;
    o.emission  = g.emission;
    o.roughness = g.roughness;
    o.current   = clipPosition(g.p, aa[2].x, aa[4].z, aa[4].x);
    o.previous  = clipPosition(geometry(id, aa[2].w).p, aa[2].y, aa[4].w, aa[4].y);
    o.position  = o.current;
    o.position.xy += aa[1].xy * 2 / aa[0].xy * float2(1, -1) * o.position.w;
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
    float3      n = normalize(i.normal), v = normalize(cameraEye(aa[2].x, aa[4].z, aa[4].x) - i.world),
           color = i.color * .025 + i.emission;
    [unroll] for (uint k = 0; k < 6; k++)
    {
        float3 delta     = float3(-2.5 + k, .6, -2.4) - i.world;
        float  distance2 = max(.15, dot(delta, delta));
        float3 l = normalize(delta), h = normalize(l + v);
        float  power = lerp(180, 4, i.roughness);
        float  spec  = pow(saturate(dot(n, h)), power) * (power + 2) / 48;
        color += (i.color * saturate(dot(n, l)) + .16 * spec) * lampColor(k) * (3 + k) / distance2;
    }
    color *= aa[14].z * aa[2].z;
    o.color    = rgbL(color);
    float2 uv  = i.current.xy / i.current.w * float2(.5, -.5) + .5,
           old = i.previous.xy / i.previous.w * float2(.5, -.5) + .5;
    o.motion   = float4(old - uv, i.current.w / 16, .125);
    return o;
}
#else
struct Varying
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
Varying VSMain(uint id : SV_VertexID)
{
    Varying o;
    o.uv       = float2((id << 1) & 2, id & 2);
    o.position = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float3 prefilter(float3 c)
{
    float b = max(c.r, max(c.g, c.b)), k = max(aa[12].y * aa[12].z, .0001), soft = clamp(b - aa[12].y + k, 0, 2 * k);
    soft = soft * soft / (4 * k);
    return c * max(b - aa[12].y, soft) / max(b, .0001);
}
float4 PSMain(Varying i)
  : SV_Target
{
#if AA_PASS == 11
    uint w, h;
    colorTex.GetDimensions(w, h);
    int2   base  = int2(i.position.xy) * 2;
    float3 sum   = 0;
    float  count = 0;
    [unroll] for (int y = 0; y < 2; y++)[unroll] for (int x = 0; x < 2; x++)
    {
        int2 p = base + int2(x, y);
        if (p.x < int(w) && p.y < int(h))
        {
            float3 c = colorTex.Load(int3(p, 0)).rgb;
            sum += aa[15].y > .5 ? prefilter(c) : c;
            count++;
        }
    }
    return float4(sum / max(count, 1), 1);
#elif AA_PASS == 12
    uint w, h;
    bloomTex.GetDimensions(w, h);
    float3 up = 0;
    [unroll] for (int y = -1; y <= 1; y++)[unroll] for (int x = -1; x <= 1; x++)
    {
        float weight = (x == 0 ? 2 : 1) * (y == 0 ? 2 : 1);
        up += bloomTex.SampleLevel(LinearSampler, i.uv + float2(x, y) / float2(w, h), 0).rgb * weight / 16;
    }
    return float4(colorTex.SampleLevel(LinearSampler, i.uv, 0).rgb + up * .5, 1);
#elif AA_PASS == 13
    uint w, h;
    colorTex.GetDimensions(w, h);
    int2   base = int2(i.position.xy) * 4;
    float2 sum  = 0;
    [unroll] for (int y = 0; y < 4; y++)[unroll] for (int x = 0; x < 4; x++)
    {
        int2 p = base + int2(x, y);
        if (p.x < int(w) && p.y < int(h))
        {
            float4 c = colorTex.Load(int3(p, 0));
            sum += aa[15].y < .5 ? float2(log(max(luminance(c.rgb), .0001)), 1) : c.rg;
        }
    }
    return float4(sum / 16, 0, 1);
#elif AA_PASS == 14
    float2 data   = colorTex.Load(int3(0, 0, 0)).rg;
    float  mean   = exp(data.r / max(data.g, .0000001));
    float  target = clamp(.18 / max(mean, .0001), exp2(aa[14].x), exp2(aa[14].y));
    float  old    = bloomTex.Load(int3(0, 0, 0)).r;
    return float4(aa[15].x > .5 ? lerp(old, target, 1 - exp(-aa[13].z * aa[13].w)) : target, 0, 0, 1);
#else
    float3 c = colorTex.SampleLevel(LinearSampler, i.uv, 0).rgb;
    if (aa[12].x > .5)
        c += bloomTex.SampleLevel(LinearSampler, i.uv, 0).rgb * aa[12].w;
    float exposure = exp2(aa[11].z) * (aa[11].w > .5 ? exposureTex.Load(int3(0, 0, 0)).r : 1);
    c              = max(c, 0) * exposure;
    if (aa[11].y > 3.5)
    {
        // Match the reference viewer's display contrast after the filmic transform.
        c = (acesFilmicToneMap(c) - .5) * aa[13].y + .5;
        return rgbL(saturate(lerp(luminance(c).xxx, c, aa[13].x)));
    }
    c              = lerp(luminance(c).xxx, c, aa[13].x);
    c              = .18 * pow(max(c, 0) / .18, aa[13].y);
    if (aa[11].y < .5)
        c = saturate(c);
    else if (aa[11].y < 1.5)
        c = c / (1 + c);
    else if (aa[11].y < 2.5)
        c = saturate((c * (2.51 * c + .03)) / (c * (2.43 * c + .59) + .14));
    else
        c = agxToneMap(c);
    return rgbL(c);
#endif
}
#endif
