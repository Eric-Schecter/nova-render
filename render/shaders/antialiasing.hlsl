#include "texture-filtering.hlsli"
// Nova AA laboratory. Scene geometry and all reconstruction passes are shared by native and web.
cbuffer AAParameters : register(b0)
{
    float4 aa[16];
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
#if AA_PASS == 30
float3 reflectionNormal(float2 oct)
{
    float3 n = float3(oct, 1 - abs(oct.x) - abs(oct.y));
    float t = saturate(-n.z);
    n.xy += float2(n.x >= 0 ? -t : t, n.y >= 0 ? -t : t);
    return normalize(n);
}
float reflectionLuma(float3 radiance)
{
    return dot(radiance, float3(.2126, .7152, .0722));
}
float4 reflectionSpatial(int2 lowPixel, int2 last, float4 current, float4 depth,
                         float4 coat, float3 normal, float roughness)
{
    float roughWeight = smoothstep(.24, .65, roughness);
    if (roughWeight <= 0)
        return current;

    // Estimate local luminance variance from taps that agree with the center surface.
    int stride = roughness >= .55 ? 2 : 1;
    float3 sum = current.rgb * 4;
    float centerLuma = reflectionLuma(current.rgb);
    float2 moments = float2(centerLuma, centerLuma * centerLuma) * 4;
    float mass = 4;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
                continue;
            int2 tap = lowPixel + int2(x, y) * stride;
            if (any(tap < 1) || any(tap > last))
                continue;
            int3 guide = int3(tap * 2 - 1, 0);
            float4 sampleValue = colorTex.Load(int3(tap, 0));
            float4 tapDepth = areaTex.Load(guide);
            float dz = abs((depth.x - tapDepth.x) + (depth.y - tapDepth.y));
            if (sampleValue.a <= 0 || abs(depth.w - tapDepth.w) > .5 || dz > .045)
                continue;

            float4 tapCoat = aa[2].x > .5 ? searchTex.Load(guide) : 0;
            bool coated = coat.z > 0;
            if (abs(coat.z - tapCoat.z) > .1 || (coated && abs(coat.w - tapCoat.w) > .1))
                continue;
            float tapRoughness = weightTex.Load(guide).a;
            if (coated)
                tapRoughness = min(tapRoughness, tapCoat.w);
            float dr = abs(tapRoughness - roughness);
            float2 tapNormal = coated ? tapCoat.xy : edgeTex.Load(guide).xy;
            float alignment = dot(normal, reflectionNormal(tapNormal));
            if (dr > .12 || alignment < .9)
                continue;

            float weight = (x == 0 || y == 0 ? 2 : 1) * exp2(-125 * dz - 18 * dr)
                           * pow(saturate(alignment), 24);
            float luma = reflectionLuma(sampleValue.rgb);
            sum += sampleValue.rgb * weight;
            moments += float2(luma, luma * luma) * weight;
            mass += weight;
        }
    moments /= mass;
    float deviation = sqrt(max(0, moments.y - moments.x * moments.x));
    float filterWeight = roughWeight * saturate(deviation / (.015 + .2 * moments.x));
    return float4(lerp(current.rgb, sum / mass, filterWeight), current.a);
}

float3 reflectionCameraRay(float2 pixel, int2 extent)
{
    float2 ndc = (pixel - .5) / float2(extent) * float2(2, -2) + float2(-1, 1);
    return aa[14].xyz + ndc.x * aa[12].w * aa[12].xyz + ndc.y * aa[13].w * aa[13].xyz;
}

bool reflectionVirtualHistory(int2 lowPixel, int2 extent, float4 depth, float encodedDistance,
                              inout float2 previousPixel, inout float previousDepth)
{
    if (aa[2].z < .5 || encodedDistance <= 1 || aa[13].w < 0
        || all(abs(aa[11].xyz - aa[15].xyz) < 1e-6))
        return false;

    int2 pixel = lowPixel * 2 - 1;
    float3 normal = reflectionNormal(edgeTex.Load(int3(pixel, 0)).zw);
    float3 ray = reflectionCameraRay(pixel, extent);
    float primaryDepth = exp2(depth.x + depth.y);
    float3 surface = ray * primaryDepth;

    // Test neighbors along both axes against the center tangent plane.
    [unroll] for (int axis = 0; axis < 2; ++axis)
    {
        int2 offset = axis == 0 ? int2(2, 0) : int2(0, 2);
        int2 tap = pixel + offset;
        if (any(tap > extent))
            tap = pixel - offset;
        if (any(tap < 1))
            return false;
        float4 neighbor = areaTex.Load(int3(tap, 0));
        float3 tapRay = reflectionCameraRay(tap, extent);
        float3 tapSurface = tapRay * exp2(neighbor.x + neighbor.y);
        if (abs(depth.w - neighbor.w) > .5
            || abs(dot(normal, tapSurface - surface)) > max(.002, primaryDepth * .002)
            || dot(normal, reflectionNormal(edgeTex.Load(int3(tap, 0)).zw)) < .998)
            return false;
    }

    float distance = exp2(encodedDistance - 1) - 1;
    float3 virtualPoint = aa[11].xyz + surface + normalize(ray) * distance;
    float4 clip = aa[7] * virtualPoint.x + aa[8] * virtualPoint.y + aa[9] * virtualPoint.z + aa[10];
    float3 virtualRay = virtualPoint - aa[15].xyz;
    float denominator = dot(normal, virtualRay);
    if (clip.w <= 0 || abs(denominator) < 1e-5)
        return false;
    float t = dot(normal, aa[11].xyz + surface - aa[15].xyz) / denominator;
    if (t <= 0 || t >= 1)
        return false;
    float2 uv = clip.xy / clip.w * float2(.5, -.5) + .5;
    previousPixel = (uv * float2(extent) - .5 + aa[1].zw) * .5 + 1;
    previousDepth = log2(max(1e-20, clip.w * t));
    return true;
}

float4 hybridAOSpatial(int2 pixel, int2 last)
{
    float4 center = colorTex.Load(int3(pixel, 0));
    if (center.a <= 0 || any(pixel < 1) || any(pixel > last))
        return center;
    int3 guide = int3(pixel * 2 - 1, 0);
    float2 depth = areaTex.Load(guide).xy;
    float3 normal = reflectionNormal(edgeTex.Load(guide).zw);
    float instance = motionTex.Load(guide).w;
    float sum = center.x * 4, mass = 4;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
                continue;
            int2 tap = pixel + int2(x, y);
            if (any(tap < 1) || any(tap > last))
                continue;
            float4 value = colorTex.Load(int3(tap, 0));
            int3 neighbor = int3(tap * 2 - 1, 0);
            float2 neighborDepth = areaTex.Load(neighbor).xy;
            float dz = abs((depth.x - neighborDepth.x) + (depth.y - neighborDepth.y));
            float alignment = dot(normal, reflectionNormal(edgeTex.Load(neighbor).zw));
            if (value.a <= 0 || dz > .035 || alignment < .94
                || abs(instance - motionTex.Load(neighbor).w) > .25)
                continue;
            // Preserve narrow contacts even on the same plane. Limit how far
            // a tap can pull the estimate across a visibility discontinuity.
            float weight = (x == 0 || y == 0 ? 2 : 1)
                * exp2(-96 * dz - 32 * (1 - alignment) - 6 * abs(value.x - center.x));
            sum += value.x * weight;
            mass += weight;
        }
    return float4(sum / mass, center.yzw);
}

float4 hybridShadowUpsample(int2 pixel)
{
    float4 current = colorTex.Load(int3(pixel, 0));
    if (current.x >= 0)
        return current;
    if (all(((pixel - 1) & 1) == 0))
        return historyTex.Load(int3((pixel + 1) / 2, 0));
    uint width, height;
    areaTex.GetDimensions(width, height);
    int2 last = ((int2(width, height) - 1) / 2) * 2 - 1;
    int2 first = ((pixel - 1) / 2) * 2 + 1;
    float2 fraction = float2((pixel - 1) & 1) * .5;
    float4 receiver = areaTex.Load(int3(pixel, 0));
    float3 normal = reflectionNormal(edgeTex.Load(int3(pixel, 0)).zw);
    float4 sum = 0;
    float mass = 0;
    [unroll] for (int y = 0; y < 2; ++y)
        [unroll] for (int x = 0; x < 2; ++x)
        {
            int3 tap = int3(min(first + int2(x, y) * 2, last), 0);
            float4 guide = areaTex.Load(tap);
            float depthDelta = abs((receiver.x - guide.x) + (receiver.y - guide.y));
            float agreement = dot(normal, reflectionNormal(edgeTex.Load(tap).zw));
            if (weightTex.Load(tap).w <= 0 || depthDelta >= .018 || abs(receiver.w - guide.w) >= .5
                || agreement <= .94)
                continue;
            float2 spatial = lerp(1 - fraction, fraction, float2(x, y));
            float weight = (spatial.x * spatial.y + .02) * exp2(-120 * depthDelta) * agreement;
            sum += historyTex.Load(int3((tap.xy + 1) / 2, 0)) * weight;
            mass += weight;
        }
    return mass > 0 ? sum / mass : 1;
}

float4 hybridAOResolve(int2 pixel)
{
    uint width, height;
    areaTex.GetDimensions(width, height);
    int2 extent = int2(width, height) - 2;
    int2 last = (extent + 1) / 2;
    if (aa[2].w > 4.5)
        return hybridShadowUpsample(pixel);
    if (aa[2].w > 3.5)
        return hybridAOSpatial(pixel, last);
    if (aa[2].w > 2.5)
    {
        float4 position = areaTex.Load(int3(pixel, 0));
        if (any(pixel < 1) || any(pixel > extent) || weightTex.Load(int3(pixel, 0)).w <= 0)
            return position;
        float2 low = (float2(pixel) - 1) * .5 + 1;
        int2 first = int2(low);
        float2 f = frac(low);
        float3 normal = reflectionNormal(edgeTex.Load(int3(pixel, 0)).zw);
        float weighted = 0, mass = 0;
        [unroll] for (int y = 0; y <= 1; ++y)
            [unroll] for (int x = 0; x <= 1; ++x)
            {
                int2 tap = min(first + int2(x, y), last);
                int3 guide = int3(tap * 2 - 1, 0);
                float4 ao = colorTex.Load(int3(tap, 0));
                float4 depth = areaTex.Load(guide);
                float dz = abs((position.x - depth.x) + (position.y - depth.y));
                float alignment = dot(normal, reflectionNormal(edgeTex.Load(guide).zw));
                if (ao.a <= 0 || dz > .055 || alignment < .9 || abs(position.w - depth.w) > .5)
                    continue;
                float w = max(.025, (x == 0 ? 1 - f.x : f.x) * (y == 0 ? 1 - f.y : f.y));
                w *= exp2(-120 * dz) * pow(saturate(alignment), 16);
                weighted += ao.x * w;
                mass += w;
            }
        if (mass > .001)
            position.z *= saturate(weighted / mass);
        return position;
    }

    if (any(pixel < 1) || any(pixel > last))
        return 0;
    int3 guide = int3(pixel * 2 - 1, 0);
    float2 encoded = edgeTex.Load(guide).zw;
    float4 motion = motionTex.Load(guide);
    float4 position = areaTex.Load(guide);
    if (aa[2].w > 1.5)
        return float4(encoded, position.x + position.y, motion.w);
    float4 current = colorTex.Load(int3(pixel, 0));
    if (current.a <= 0 || aa[3].x < .5 || motion.w < .5)
        return current;
    float2 previousPixel = float2(pixel) + motion.xy * float2(extent) * .5;
    int2 first = int2(floor(previousPixel));
    float2 f = frac(previousPixel);
    float weighted = 0, mass = 0, age = 0;
    float3 normal = reflectionNormal(encoded);
    [unroll] for (int y = 0; y <= 1; ++y)
        [unroll] for (int x = 0; x <= 1; ++x)
        {
            int2 tap = first + int2(x, y);
            if (any(tap < 1) || any(tap > last))
                continue;
            float4 old = oldMotionTex.Load(int3(tap, 0));
            float4 value = historyTex.Load(int3(tap, 0));
            if (value.a <= 0 || abs(old.w - motion.w) > .25 || abs(old.z - motion.z) > .04
                || dot(normal, reflectionNormal(old.xy)) < .92)
                continue;
            float w = (x == 0 ? 1 - f.x : f.x) * (y == 0 ? 1 - f.y : f.y);
            weighted += value.x * w;
            age += value.a * w;
            mass += w;
        }
    if (mass < .2)
        return current;
    age /= mass;
    return float4(lerp(current.x, weighted / mass, min(.85, age / (age + 1))), 0, 0, min(age + 1, 16));
}

float4 reflectionAtrous(int2 pixel, int2 last, float4 current, float4 depth, float4 coat,
                         float3 normal, float roughness)
{
    if (roughness < .15)
        return current;

    int step = int(aa[4].w) - 1;
    float2 moments = historyTex.Load(int3(pixel, 0)).xy;
    float variance = max(0, moments.y - moments.x * moments.x);
    float center = reflectionLuma(current.rgb);
    float minimumTolerance = .02 + center * .06;
    float3 sum = current.rgb * 4;
    float mass = 4;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
                continue;
            int2 tap = pixel + int2(x, y) * step;
            if (any(tap < 1) || any(tap > last))
                continue;
            float4 color = colorTex.Load(int3(tap, 0));
            int3 guide = int3(tap * 2 - 1, 0);
            float4 tapDepth = areaTex.Load(guide);
            float dz = abs((depth.x - tapDepth.x) + (depth.y - tapDepth.y));
            if (color.a < 1 || dz > .055 || abs(depth.w - tapDepth.w) > .5)
                continue;
            float4 tapCoat = aa[2].x > .5 ? searchTex.Load(guide) : 0;
            if (abs(tapCoat.z - coat.z) > .1 || (coat.z > 0 && abs(tapCoat.w - coat.w) > .1))
                continue;
            float tapRoughness = weightTex.Load(guide).a;
            if (coat.z > 0)
                tapRoughness = min(tapRoughness, tapCoat.w);
            float dr = abs(tapRoughness - roughness);
            float2 encoded = coat.z > 0 ? tapCoat.xy : edgeTex.Load(guide).xy;
            float alignment = dot(normal, reflectionNormal(encoded));
            if (alignment < .92 || dr > .12)
                continue;
            // A stable-looking center can have zero variance when sparse
            // glossy samples repeatedly miss. Use the same-surface tap's
            // moments too, without borrowing uncertainty across an edge.
            float2 tapMoments = historyTex.Load(int3(tap, 0)).xy;
            float tapVariance = max(0, tapMoments.y - tapMoments.x * tapMoments.x);
            float tolerance = max(minimumTolerance, 1.5 * sqrt(.5 * (variance + tapVariance)));
            float weight = (x == 0 || y == 0 ? 2 : 1)
                * exp2(-120 * dz / step - 18 * dr - abs(reflectionLuma(color.rgb) - center) / tolerance)
                * pow(saturate(alignment), 32);
            sum += color.rgb * weight;
            mass += weight;
        }
    return float4(lerp(current.rgb, sum / mass, smoothstep(.15, .65, roughness)), current.a);
}

float4 reflectionTemporal(int2 lowPixel)
{
    uint width, height;
    areaTex.GetDimensions(width, height);
    int2 extent = int2(width, height) - 2;
    int2 last = (extent + 1) / 2;
    if (any(lowPixel < 1) || any(lowPixel > last))
        return 0;
    int3 pixel = int3(lowPixel * 2 - 1, 0);
    float4 depth = areaTex.Load(pixel), motion = motionTex.Load(pixel);
    float4 coat = aa[2].x > .5 ? searchTex.Load(pixel) : 0;
    bool coated = coat.z > 0;
    float2 encodedNormal = coated ? coat.xy : edgeTex.Load(pixel).xy;
    float currentDepth = depth.x + depth.y;
    if (aa[6].w > .5)
        return float4(encodedNormal, currentDepth, motion.w);

    float4 current = colorTex.Load(int3(lowPixel, 0));
    if (current.a <= 0)
        return 0;
    bool momentsPass = aa[4].w > .5 && aa[4].w < 1.5;
    if (current.a < 1)
        return momentsPass ? 0 : current;
    float roughness = weightTex.Load(pixel).a;
    if (coated)
        roughness = min(roughness, coat.w);
    float3 normal = reflectionNormal(encodedNormal);

    if (aa[4].w > 1.5)
        return reflectionAtrous(lowPixel, last, current, depth, coat, normal, roughness);

    if (aa[2].y > .5)
        return reflectionSpatial(lowPixel, last, current, depth, coat, normal, roughness);

    // Use nearby samples from the same surface to limit isolated fireflies on rough materials.
    float3 neighbors = 0, upper = current.rgb;
    float count = 0;
    const int2 offsets[4] = {int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)};
    [unroll] for (int i = 0; i < 4; ++i)
    {
        int2 tap = lowPixel + offsets[i];
        if (any(tap < 1) || any(tap > last))
            continue;
        float4 sampleValue = colorTex.Load(int3(tap, 0));
        int3 guide = int3(tap * 2 - 1, 0);
        float4 tapDepth = areaTex.Load(guide);
        if (sampleValue.a <= 0 || abs(tapDepth.x + tapDepth.y - currentDepth) > .06
            || abs(tapDepth.w - depth.w) > .5)
            continue;
        float4 tapCoat = aa[2].x > .5 ? searchTex.Load(guide) : 0;
        if (abs(tapCoat.z - coat.z) > .1 || (coated && abs(tapCoat.w - coat.w) > .1))
            continue;
        float2 tapNormal = coated ? tapCoat.xy : edgeTex.Load(guide).xy;
        if (dot(normal, reflectionNormal(tapNormal)) < .9)
            continue;
        neighbors += sampleValue.rgb;
        upper = max(upper, sampleValue.rgb);
        count += 1;
    }
    if (count >= 2)
    {
        float3 mean = neighbors / count;
        float ceiling = max(.025, 4 * reflectionLuma(mean));
        float suppression = smoothstep(.25, .65, roughness);
        current.rgb *= lerp(1, min(1, ceiling / max(.0001, reflectionLuma(current.rgb))), suppression);
        upper = lerp(upper, min(upper, max(current.rgb, mean * 4 + .025)), suppression);
    }

    float luma = min(128, reflectionLuma(current.rgb));
    float3 sample = momentsPass ? float3(luma, luma * luma, 0) : current.rgb;
    if (aa[3].x < .5 || motion.w < .5)
        return float4(sample, 1);
    float2 previousPixel = float2(lowPixel) + (motion.xy * float2(extent) + aa[1].zw - aa[1].xy) * .5;
    float previousDepth = motion.z;
    reflectionVirtualHistory(lowPixel, extent, depth, current.a, previousPixel, previousDepth);
    int2 first = int2(floor(previousPixel));
    float2 fracPixel = frac(previousPixel);
    float3 sum = 0;
    float mass = 0, age = 0;
    [unroll] for (int y = 0; y <= 1; ++y)
        [unroll] for (int x = 0; x <= 1; ++x)
        {
            int2 tap = first + int2(x, y);
            if (any(tap < 1) || any(tap > last))
                continue;
            float4 guide = oldMotionTex.Load(int3(tap, 0));
            if (abs(guide.w - motion.w) > .25 || abs(guide.z - previousDepth) > .045
                || dot(normal, reflectionNormal(guide.xy)) < .9)
                continue;
            float4 history = historyTex.Load(int3(tap, 0));
            if (history.a < 1)
                continue;
            float blend = (x ? fracPixel.x : 1 - fracPixel.x) * (y ? fracPixel.y : 1 - fracPixel.y);
            sum += history.rgb * blend;
            age += history.a * blend;
            mass += blend;
        }
    if (mass < .15)
        return float4(sample, 1);
    age /= mass;
    float3 historyColor = min(sum / mass, max(upper * 1.35, current.rgb + .015));
    float strength = lerp(.65, .9, smoothstep(.15, .65, roughness));
    float weight = min(strength, age / (age + 1)) * saturate(mass * 2);
    return float4(lerp(sample, momentsPass ? sum / mass : historyColor, weight), min(age + 1, 16));
}
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
    // Reproject light visibility before deferred lighting, independently of HDR color.
    if (aa[6].w > 2.5)
    {
        int2 pixel = int2(i.position.xy);
        float4 current = colorTex.Load(int3(pixel, 0));
        float4 motion = motionTex.Load(int3(pixel, 0));
        float2 activeSize = aa[0].xy * aa[2].x;
        if (aa[4].w > 1.5)
        {
            if (motion.w < .5 || any(pixel < 1) || any(pixel > int2(activeSize)))
                return current;
            float4 mean = weightTex.Load(int3(pixel, 0));
            float4 second = historyTex.Load(int3(pixel, 0));
            float4 variance = max(0, second - mean * mean);
            float4 strength = saturate(variance * 64);
            if (all(strength <= .001))
                return current;
            const int step = int(aa[4].w) - 1;
            float4 depth = areaTex.Load(int3(pixel, 0));
            float2 encoded = edgeTex.Load(int3(pixel, 0)).zw;
            float4 sum = current * 4, mass = 4;
            [unroll] for (int y = -1; y <= 1; ++y)
                [unroll] for (int x = -1; x <= 1; ++x)
                {
                    if (x == 0 && y == 0)
                        continue;
                    int2 tap = pixel + int2(x, y) * step;
                    if (any(tap < 1) || any(tap > int2(activeSize)))
                        continue;
                    float4 oldMotion = motionTex.Load(int3(tap, 0));
                    float4 neighbor = areaTex.Load(int3(tap, 0));
                    float dz = abs((depth.x - neighbor.x) + (depth.y - neighbor.y));
                    float dn = length(encoded - edgeTex.Load(int3(tap, 0)).zw);
                    if (dz > .045 || dn > .12 || abs(oldMotion.w - motion.w) > .25)
                        continue;
                    float4 value = colorTex.Load(int3(tap, 0));
                    float4 weight = (x == 0 || y == 0 ? 2 : 1)
                        * exp2(-120 * dz / step - 24 * dn - abs(value - current) / (.03 + 2 * sqrt(variance)));
                    sum += value * weight;
                    mass += weight;
                }
            return lerp(current, sum / mass, strength);
        }
        if (aa[4].w > .5)
            current *= current;
        if (aa[3].x < .5 || motion.w < .5)
            return current;
        float2 previousPixel = float2(pixel) + motion.xy * activeSize;
        int2 first = int2(floor(previousPixel));
        float2 fraction = frac(previousPixel);
        float4 total = 0;
        float mass = 0;
        float2 normal = edgeTex.Load(int3(pixel, 0)).zw;
        [unroll] for (int y = 0; y < 2; ++y)
            [unroll] for (int x = 0; x < 2; ++x)
            {
                int2 tap = first + int2(x, y);
                if (any(tap < 1) || any(tap > int2(activeSize)))
                    continue;
                float4 oldMotion = oldMotionTex.Load(int3(tap, 0));
                float2 oldDepth = searchTex.Load(int3(tap, 0)).xy;
                if (abs(oldDepth.x + oldDepth.y - motion.z) > .026 ||
                    abs(oldMotion.w - motion.w) > .25 ||
                    length(weightTex.Load(int3(tap, 0)).zw - normal) > .13)
                    continue;
                float weight = (x == 0 ? 1 - fraction.x : fraction.x)
                             * (y == 0 ? 1 - fraction.y : fraction.y);
                total += weight * historyTex.Load(int3(tap, 0));
                mass += weight;
            }
        if (mass < .2)
            return current;
        float historyWeight = .85 * saturate(mass * 2);
        return lerp(current, total / mass, historyWeight);
    }
    // Hybrid history is reconstructed before post processing and final AA.
    // Current/old oct normals, split-log depth and motion identity reject disocclusions.
    if (aa[6].w > 1.5)
    {
        float4 current = colorTex.SampleLevel(PointSampler, uv, 0), motion = motionTex.SampleLevel(PointSampler, uv, 0);
        float2 prevUV = uv + motion.xy;
        if (aa[3].x < .5 || motion.w < .5 || any(prevUV < 0) || any(prevUV > 1))
            return current;
        float4 oldMotion = oldMotionTex.SampleLevel(PointSampler, prevUV, 0);
        float2 depth     = searchTex.SampleLevel(PointSampler, prevUV, 0).xy;
        float  expected = exp2(motion.z), previous = exp2(depth.x + depth.y);
        if (abs(previous - expected) > max(.002, .02 * expected) || abs(oldMotion.w - motion.w) > .25)
            return current;
        float2 cn = edgeTex.SampleLevel(PointSampler, uv, 0).xy * 2 - 1,
               pn = weightTex.SampleLevel(PointSampler, prevUV, 0).xy * 2 - 1;
        float3 n = float3(cn, 1 - abs(cn.x) - abs(cn.y)), p = float3(pn, 1 - abs(pn.x) - abs(pn.y));
        if (n.z < 0)
            n.xy = (1 - abs(n.yx)) * float2(n.x >= 0 ? 1 : -1, n.y >= 0 ? 1 : -1);
        if (p.z < 0)
            p.xy = (1 - abs(p.yx)) * float2(p.x >= 0 ? 1 : -1, p.y >= 0 ? 1 : -1);
        if (dot(normalize(n), normalize(p)) < .9)
            return current;
        float3 lo = current.rgb, hi = current.rgb;
        [unroll] for (int y = -1; y <= 1; ++y)[unroll] for (int x = -1; x <= 1; ++x)
        {
            float3 c = colorTex.SampleLevel(PointSampler, uv + float2(x, y) * texel, 0).rgb;
            lo       = min(lo, c);
            hi       = max(hi, c);
        }
        float3 history = clamp(historyTex.SampleLevel(LinearSampler, prevUV, 0).rgb, lo, hi);
        float  weight  = aa[3].y * saturate(1 - length(motion.xy * aa[0].xy) / 64);
        return float4(lerp(current.rgb, history, weight), current.a);
    }
    float4 current = colorTex.SampleLevel(PointSampler, uv, 0), m = motionTex.SampleLevel(PointSampler, uv, 0);
    float2 prevUV = uv + m.xy + (aa[1].zw - aa[1].xy) / aa[0].xy;
    if (aa[3].x < .5 || any(prevUV < 0) || any(prevUV > 1))
        return current;
    if (aa[6].w > .5 && abs(current.a - historyTex.SampleLevel(LinearSampler, prevUV, 0).a) > .25)
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
    float3 history  = clamp(historyTex.SampleLevel(LinearSampler, prevUV, 0).rgb, lo, hi);
    float3 resolved = lerp(current.rgb, history, aa[3].y);
    return aa[6].w > .5 ? float4(resolved, current.a) : rgbL(resolved);
#elif AA_PASS == 30
    if (aa[2].w > .5)
        return hybridAOResolve(int2(i.position.xy));
    return reflectionTemporal(int2(i.position.xy));
#elif AA_PASS == 8
    uv       = .5 + (uv - .5) / aa[3].z;
    float3 c = colorTex.SampleLevel(PointSampler, uv, 0).rgb;
    if (aa[3].w > .5 && i.uv.x < .5)
        c = historyTex.SampleLevel(PointSampler, uv, 0).rgb;
    if (aa[3].w > .5 && abs(i.uv.x - .5) < texel.x)
        c = float3(.2, .5, .8);
    // Each mask is rasterized with fresh depth from only one highlight group.
    // Resolve after temporal AA and tone mapping so the overlay has no history.
    [loop] for (int group = aa[6].w == 5 ? 1 : -1; group >= 0; --group)
    {
        float4 style = aa[12 + group];
        if (style.w <= 0)
            continue;
        float coverage = group ? weightTex.SampleLevel(PointSampler, uv, 0).a
                               : edgeTex.SampleLevel(PointSampler, uv, 0).a;
        if (coverage <= 0)
            continue;
        const int2 neighbors[8] = {
          int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1),
          int2(-1, -1), int2(1, -1), int2(-1, 1), int2(1, 1)};
        bool border = false;
        [loop] for (int step = 1; step <= int(style.w) && !border; ++step)
        {
            [unroll] for (int j = 0; j < 8; ++j)
            {
                float2 tap = uv + float2(neighbors[j] * step) * texel;
                float value = group ? weightTex.SampleLevel(PointSampler, tap, 0).a
                                    : edgeTex.SampleLevel(PointSampler, tap, 0).a;
                border = border || value <= 0;
            }
        }
        if (border)
        {
            c = style.rgb;
            break;
        }
    }
    return float4(aa[6].z > .5 ? encodeSRGB(c) : gammaRGB(c), 1);
#endif
}
#endif
