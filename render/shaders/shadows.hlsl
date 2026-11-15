// Procedural shadow showcase. Pass 9 rasterizes light-space depth; pass 10 shades the scene.
cbuffer Parameters : register(b0)
{
    float4 aa[16];
}
Texture2D<float4> shadowMap : register(t1);
SamplerState      LinearSampler : register(s9);
SamplerState      PointSampler : register(s10);
#ifndef AA_PASS
#define AA_PASS 10
#endif
static const float PI = 3.14159265359;
struct Geometry
{
    float3 position;
    float3 normal;
    float3 color;
};
float3 corner(uint id)
{
    return float3(id & 1, (id >> 1) & 1, (id >> 2) & 1) * 2 - 1;
}
Geometry geometry(uint id, float time)
{
    Geometry g;
    if (id < 6)
    {
        const uint indices[6] = {0, 1, 3, 0, 3, 2};
        uint       k          = indices[id];
        g.position            = float3((k & 1) ? 3.3 : -3.3, -1, (k & 2) ? 3.3 : -3.3);
        g.normal              = float3(0, 1, 0);
        g.color               = float3(.68, .71, .73);
        return g;
    }
    id -= 6;
    if (id < 19 * 36)
    {
        uint   object = id / 36, face = (id % 36) / 6, vertex = id % 6;
        float3 center, size;
        if (object < 4)
        {
            float h = .16 + object * .18;
            center  = float3(-1.8 + object * .65, -1 + h * .5, -.7);
            size    = float3(.6, h, .95);
            g.color = float3(.65, .61, .51);
        }
        else if (object < 16)
        {
            center  = float3(-2.5 + (object - 4) * .45, -.25, 1.5);
            size    = float3(.055, 1.5, .055);
            g.color = float3(.16, .24, .32);
        }
        else
        {
            float k = object - 16;
            center  = float3(-1.7 + k * 1.65, -.45 + k * .6 + sin(time) * .08, -.05);
            size    = float3(.48, .48, .48);
            g.color = float3(.75, .31, .12);
        }
        const uint triangles[6] = {0, 1, 3, 0, 3, 2};
        uint       q            = triangles[vertex];
        float2     uv           = float2(q & 1, (q >> 1) & 1) * 2 - 1;
        float      sign         = (face & 1) ? 1 : -1;
        uint       axis         = face / 2;
        g.normal                = axis == 0 ? float3(sign, 0, 0) : axis == 1 ? float3(0, sign, 0) : float3(0, 0, sign);
        float3 p   = axis == 0 ? float3(sign, uv) : axis == 1 ? float3(uv.x, sign, uv.y) : float3(uv, sign);
        g.position = center + p * size * .5;
        return g;
    }
    id -= 19 * 36;
    uint       object = id / 2304, k = id % 2304, cell = k / 6, vertex = k % 6;
    const uint tri[6] = {0, 1, 3, 0, 3, 2};
    uint       q      = tri[vertex];
    float u = (float(cell % 24) + float(q & 1)) / 24 * 2 * PI, v = (float(cell / 24) + float((q >> 1) & 1)) / 16 * PI;
    g.normal   = float3(sin(v) * cos(u), cos(v), sin(v) * sin(u));
    g.position = float3(-1.6 + object * 1.6, -.5 + object * .5, -1.65) + g.normal * .4;
    g.color    = object == 0 ? float3(.66, .45, .12) : object == 1 ? float3(.12, .43, .66) : float3(.19, .5, .3);
    return g;
}
float4 cameraClip(float3 p, float yaw, float pitch, float distance)
{
    float  sy = sin(yaw), cy = cos(yaw), sp = sin(pitch), cp = cos(pitch);
    float3 right = float3(cy, 0, -sy), up = float3(-sy * sp, cp, -cy * sp), forward = float3(sy * cp, sp, cy * cp);
    float3 v = float3(dot(p, right), dot(p, up), distance - dot(p, forward));
    return float4(v.x * 1.85 / (aa[0].x / aa[0].y), v.y * 1.85, v.z * .99 - .1, v.z);
}
float4 lightClip(float3 p)
{
    float3 light = normalize(aa[9].xyz), reference = abs(light.y) > .98 ? float3(0, 0, 1) : float3(0, 1, 0);
    float3 right = normalize(cross(reference, light)), up = cross(light, right);
    return float4(dot(p, right) / 4.3, dot(p, up) / 4.3, (6 - dot(p, light)) / 12, 1);
}
struct Varying
{
    float4 position : SV_Position;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 color : TEXCOORD2;
    float4 current : TEXCOORD3;
    float4 previous : TEXCOORD4;
};
Varying VSMain(uint id : SV_VertexID)
{
    Geometry g = geometry(id, aa[0].z);
    Varying  o;
    o.world    = g.position;
    o.normal   = g.normal;
    o.color    = g.color;
    o.current  = cameraClip(g.position, aa[2].x, aa[4].z, aa[4].x);
    o.previous = cameraClip(geometry(id, aa[2].w).position, aa[2].y, aa[4].w, aa[4].y);
#if AA_PASS == 9
    o.position = lightClip(g.position);
#else
    o.position = o.current;
    o.position.xy += aa[1].xy * 2 / aa[0].xy * float2(1, -1) * o.position.w;
#endif
    return o;
}
float readDepth(float2 uv)
{
    float2 d = shadowMap.SampleLevel(PointSampler, uv, 0).rg;
    return d.x + d.y / 1024;
}
float compareDepth(float2 uv, float depth)
{
    if (any(uv < 0) || any(uv > 1))
        return 1;
    return depth <= readDepth(uv) ? 1 : 0;
}
float2 disk(uint i, uint count)
{
    float angle = i * 2.39996323;
    return float2(cos(angle), sin(angle)) * sqrt((i + .5) / count);
}
float shadowVisibility(float3 world, float3 normal)
{
    if (aa[8].x < .5)
        return 1;
    float3 light = normalize(aa[9].xyz), n = normalize(normal);
    float4 clip  = lightClip(world + n * aa[8].w * (1 - saturate(dot(n, light))));
    float2 uv    = clip.xy * float2(.5, -.5) + .5;
    float  depth = clip.z - aa[8].z;
    if (any(uv < 0) || any(uv > 1) || depth <= 0 || depth >= 1)
        return 1;
    if (aa[8].x < 1.5)
        return compareDepth(uv, depth);
    float radius = aa[10].x / aa[8].y;
    if (aa[8].x > 2.5)
    {
        float sum = 0, count = 0, search = aa[9].w / 8.6 * depth;
        [unroll] for (uint i = 0; i < 24; i++)
        {
            float2 offset  = uv + disk(i, 24) * search;
            float  blocker = readDepth(offset);
            if (all(offset >= 0) && all(offset <= 1) && blocker < depth)
            {
                sum += blocker;
                count++;
            }
        }
        if (count < .5)
            return 1;
        float blocker = sum / count;
        radius        = max(.5 / aa[8].y, aa[9].w / 8.6 * (depth - blocker) / max(.05, blocker));
    }
    float visibility = 0;
    [unroll] for (uint j = 0; j < 32; j++) visibility += compareDepth(uv + disk(j, 32) * radius, depth);
    return visibility / 32;
}
#if AA_PASS == 9
float4 PSMain(Varying input)
  : SV_Target
{
    // Split depth keeps sufficient precision in a portable RGBA16F render target.
    float d = lightClip(input.world).z, high = floor(d * 1024) / 1024;
    return float4(high, (d - high) * 1024, 0, 1);
}
#else
struct SceneOutput
{
    float4 color : SV_Target0;
    float4 motion : SV_Target1;
};
SceneOutput PSMain(Varying input)
{
    SceneOutput o;
    float3      n = normalize(input.normal), l = normalize(aa[9].xyz);
    float       visibility = shadowVisibility(input.world, n);
    float3      color      = input.color * (.22 + .95 * saturate(dot(n, l)) * visibility) * aa[2].z;
    float3      display    = pow(saturate(color), 1 / 2.2);
    o.color                = float4(color, dot(display, float3(.299, .587, .114)));
    float2 current         = input.current.xy / input.current.w * float2(.5, -.5) + .5;
    float2 previous        = input.previous.xy / input.previous.w * float2(.5, -.5) + .5;
    o.motion               = float4(previous - current, input.current.w / 16, 1.0 / 8);
    return o;
}
#endif
