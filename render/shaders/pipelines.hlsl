// Raster pipeline laboratory. One scene and lighting equation for all four paths.
// Light lists use an RGBA16F atlas (exact integer indices <= 256), produced on GPU.
cbuffer Parameters : register(b0)
{
    float4 aa[16];
}
Texture2D<float4> albedoBuffer : register(t1);
Texture2D<float4> normalBuffer : register(t2);
Texture2D<float4> positionBuffer : register(t3);
Texture2D<float4> materialBuffer : register(t4);
Texture2D<float4> depthBuffer : register(t5);
Texture2D<float4> tileBuffer : register(t6);
Texture2D<float4> listBuffer : register(t7);
#ifndef AA_PASS
#define AA_PASS 22
#endif
static const float Near = .1, Far = 64, Focal = 1.8;
void               basis(out float3 r, out float3 u, out float3 f)
{
    float sy = sin(aa[2].x), cy = cos(aa[2].x), sp = sin(aa[2].y), cp = cos(aa[2].y);
    r = float3(cy, 0, -sy);
    u = float3(-sy * sp, cp, -cy * sp);
    f = float3(sy * cp, sp, cy * cp);
}
float3 view(float3 p)
{
    float3 r, u, f;
    basis(r, u, f);
    p -= aa[3].xyz;
    return float3(dot(p, r), dot(p, u), aa[2].z - dot(p, f));
}
float4 project(float3 p)
{
    float3 v = view(p);
    return float4(
      v.x * Focal * aa[0].y / aa[0].x, v.y * Focal, Far / (Far - Near) * v.z - Near * Far / (Far - Near), v.z);
}
void light(uint id, out float3 p, out float3 c)
{
    float k = float(id);
    p       = float3(sin(k * 2.399963) * 6.8, .1 + frac(k * .618034) * 4.8, cos(k * 2.399963) * 6.8);
    c       = .3 + .7 * cos(float3(0, 2, 4) + k * .73) * cos(float3(0, 2, 4) + k * .73);
}
struct Vertex
{
    float4 position : SV_Position;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 color : TEXCOORD2;
    float2 material : TEXCOORD3;
};
Vertex VSMain(uint id : SV_VertexID)
{
    Vertex o = (Vertex)0;
#if AA_PASS == 22 || AA_PASS == 23 || AA_PASS == 27
    uint   box = id / 36, face = (id % 36) / 6, corner = id % 6;
    float2 uv = corner == 0   ? float2(-1, -1)
                : corner == 1 ? float2(1, -1)
                : corner == 2 ? float2(1, 1)
                : corner == 3 ? float2(-1, -1)
                : corner == 4 ? float2(1, 1)
                              : float2(-1, 1);
    float3 n  = face == 0   ? float3(1, 0, 0)
                : face == 1 ? float3(-1, 0, 0)
                : face == 2 ? float3(0, 1, 0)
                : face == 3 ? float3(0, -1, 0)
                : face == 4 ? float3(0, 0, 1)
                            : float3(0, 0, -1);
    float3 t  = abs(n.y) > .5 ? float3(1, 0, 0) : normalize(cross(float3(0, 1, 0), n));
    float3 b  = cross(n, t);
    float3 center, extent;
    float  k = float(box);
    if (box == 0)
    {
        center = float3(0, -1.15, 0);
        extent = float3(8, .15, 8);
    }
    else if (box == 1)
    {
        center = float3(0, 2, -8);
        extent = float3(8, 3, .15);
    }
    else
    {
        uint q   = box - 2;
        center   = float3((int(q % 7) - 3) * 1.9, -.3, (int(q / 7) - 3) * 1.9);
        extent   = float3(.48, .7 + frac(k * .37) * 1.3, .48);
        center.y = extent.y - 1;
    }
    o.world    = center + (n + t * uv.x + b * uv.y) * extent;
    o.normal   = n;
    o.color    = float4(box < 2 ? float3(.48, .5, .54) : .2 + .5 * frac(float3(.31, .57, .83) * k), 1);
    o.material = float2(.2 + .65 * frac(k * .173), box % 3 == 0 ? .65 : 0);
    o.position = project(o.world);
#else
    float2 p   = float2((id << 1) & 2, id & 2);
    o.position = float4(p * 2 - 1, 0, 1);
#endif
    return o;
}
uint slice(float z)
{
    return min(15u, (uint)max(0., floor(log(max(z, Near) / Near) / log(Far / Near) * 16)));
}
uint cellID(float2 pixel, float z)
{
    uint2 t = min(uint2(pixel / aa[0].xy * float2(16, 9)), uint2(15, 8));
    return t.y * 16 + t.x + (aa[0].z > 2.5 ? slice(z) * 144 : 0);
}
float3 illuminate(float3 p, float3 n, float3 base, float2 mat, uint id)
{
    float3 lp, lc;
    light(id, lp, lc);
    float3 d    = lp - p;
    float  dist = length(d), radius = aa[1].y;
    if (dist >= radius)
        return 0;
    float3 l  = d / max(dist, .0001);
    float  nl = saturate(dot(n, l));
    float3 r, u, f;
    basis(r, u, f);
    float3 v = normalize(aa[3].xyz + f * aa[2].z - p), h = normalize(l + v);
    float  spec = pow(saturate(dot(n, h)), lerp(128., 4., mat.x));
    float3 brdf = base * (1 - mat.y) * nl / 3.14159265 + lerp(.04.xxx, base, mat.y) * spec * nl;
    float  fall = pow(saturate(1 - pow(dist / radius, 4)), 2) / (1 + dist * dist);
    return brdf * lc * aa[4].x * fall;
}
float3 shade(float3 p, float3 n, float3 base, float2 mat, float2 pixel, out uint tested)
{
    uint   count = (uint)aa[1].x;
    float3 c     = base * .025;
    tested       = count;
    if (aa[0].z < 1.5)
    {
        for (uint i = 0; i < count; i++)
            c += illuminate(p, n, base, mat, i);
    }
    else
    {
        uint cell  = cellID(pixel, view(p).z);
        uint total = (uint)listBuffer.Load(int3(0, cell, 0)).x;
        tested     = total > 64 ? count : total;
        if (total > 64)
        {
            for (uint j = 0; j < count; j++)
                c += illuminate(p, n, base, mat, j);
        }
        else
        {
            for (uint j = 0; j < total; j++)
            {
                float4 group = listBuffer.Load(int3(1 + j / 4, cell, 0));
                uint   id    = (uint)group[j % 4] - 1;
                c += illuminate(p, n, base, mat, id);
            }
        }
    }
    return c;
}
float4 display(float3 p, float3 n, float3 base, float2 mat, float2 pixel)
{
    uint   tested;
    float3 c     = shade(p, n, base, mat, pixel, tested);
    uint   debug = (uint)aa[0].w;
    if (debug == 1)
        return float4(base, 1);
    if (debug == 2)
        return float4(n * .5 + .5, 1);
    if (debug == 3)
        return float4(saturate(view(p).z / Far).xxx, 1);
    if (debug == 4)
        return float4(mat.x.xxx, 1);
    if (debug == 5)
        return float4(mat.y.xxx, 1);
    if (debug == 6)
    {
        float v = float(tested) / max(1., aa[1].x);
        return float4(v, v * v, 1 - v, 1);
    }
    if (debug == 7)
    {
        uint total = aa[0].z < 1.5 ? 0 : (uint)listBuffer.Load(int3(0, cellID(pixel, view(p).z), 0)).x;
        return float4(total > 64 ? float3(1, 0, 1) : float3(0, .15, 0), 1);
    }
    return float4(pow(saturate(c / (1 + c)), 1 / 2.2), 1);
}
#if AA_PASS == 22
struct GBuffer
{
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
    float4 position : SV_Target2;
    float4 material : SV_Target3;
};
GBuffer PSMain(Vertex i)
{
    GBuffer o;
    o.albedo   = i.color;
    o.normal   = float4(normalize(i.normal), 1);
    o.position = float4(i.world, view(i.world).z);
    o.material = float4(i.material, 0, 1);
    return o;
}
#else
float4 PSMain(Vertex i)
  : SV_Target
{
#if AA_PASS == 28
    return albedoBuffer.Load(int3(int2(i.position.xy), 0));
#elif AA_PASS == 23
    return display(i.world, normalize(i.normal), i.color.rgb, i.material, i.position.xy);
#elif AA_PASS == 27
    return float4(view(i.world).z, 0, 0, 1);
#elif AA_PASS == 24
    int2   xy = int2(i.position.xy);
    float4 p  = positionBuffer.Load(int3(xy, 0));
    if (p.w <= 0)
        return float4(.015, .02, .03, 1);
    return display(
      p.xyz,
      normalize(normalBuffer.Load(int3(xy, 0)).xyz),
      albedoBuffer.Load(int3(xy, 0)).rgb,
      materialBuffer.Load(int3(xy, 0)).xy,
      i.position.xy);
#elif AA_PASS == 26
    uint2 tile = uint2(i.position.xy);
    uint2 lo   = (uint2)ceil(float2(tile) * aa[0].xy / float2(16, 9) - .5);
    uint2 hi   = (uint2)ceil(float2(tile + 1) * aa[0].xy / float2(16, 9) - .5);
    float zmin = Far, zmax = 0;
    for (uint y = lo.y; y < hi.y; y++)
        for (uint x = lo.x; x < hi.x; x++)
        {
            float z = depthBuffer.Load(int3(x, y, 0)).x;
            if (z > 0)
            {
                zmin = min(zmin, z);
                zmax = max(zmax, z);
            }
        }
    return float4(zmin, zmax, 0, 1);
#elif AA_PASS == 25
    uint group = (uint)i.position.x, cell = (uint)i.position.y, tile = cell % 144, zs = cell / 144;
    if (aa[0].z < 2.5 && zs > 0)
        return 0;
    float2 zr = aa[0].z > 2.5 ? Near * pow(Far / Near, float2(zs, zs + 1) / 16)
                              : tileBuffer.Load(int3(tile % 16, tile / 16, 0)).xy;
    if (zr.y <= 0)
        return 0;
    float2 lower = (float2(tile % 16, tile / 16) / float2(16, 9) * 2 - 1) * float2(aa[0].x / aa[0].y, -1) / Focal;
    float2 upper =
      (float2(tile % 16 + 1, tile / 16 + 1) / float2(16, 9) * 2 - 1) * float2(aa[0].x / aa[0].y, -1) / Focal;
    float2 amin    = min(min(lower * zr.x, lower * zr.y), min(upper * zr.x, upper * zr.y));
    float2 amax    = max(max(lower * zr.x, lower * zr.y), max(upper * zr.x, upper * zr.y));
    uint   found   = 0;
    float4 indices = 0;
    for (uint id = 0; id < (uint)aa[1].x; id++)
    {
        float3 lp, lc;
        light(id, lp, lc);
        float3 v       = view(lp);
        float3 closest = clamp(v, float3(amin, zr.x), float3(amax, zr.y));
        if (dot(v - closest, v - closest) <= aa[1].y * aa[1].y)
        {
            if (group > 0 && found / 4 == group - 1)
                indices[found % 4] = id + 1;
            found++;
        }
    }
    return group == 0 ? float4(found, 0, 0, 1) : indices;
#else
    return 0;
#endif
}
#endif
