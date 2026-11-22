#ifndef NOVA_MATERIAL_SHEEN
#define NOVA_MATERIAL_SHEEN
// Directional albedo of the Charlie distribution with Ashikhmin visibility.
// Integrated using 4096 importance-sampled half vectors at each grid coordinate.
static const uint sheenAlbedoTable[64] = {
    0x062effffu, 0x00000001u, 0x00000000u, 0x00000000u, 0x1559ffffu, 0x00000104u, 0x00000000u, 0x00000000u,
    0x5bafffffu, 0x040a162eu, 0x00000102u, 0x00000000u, 0x7abbffffu, 0x131f324fu, 0x0103060bu, 0x00000001u,
    0x83b4fdffu, 0x2231455fu, 0x060a1018u, 0x00010203u, 0x84aae1ffu, 0x2f3d5067u, 0x0d121a23u, 0x01020508u,
    0x82a1cbffu, 0x37455569u, 0x141a222cu, 0x0306090eu, 0x7e98bbf9u, 0x3e4a596au, 0x1a212933u, 0x060a0f14u,
    0x7b91aee3u, 0x424d5a69u, 0x20272f38u, 0x0a0f141au, 0x788ba4d2u, 0x454f5b68u, 0x252c333cu, 0x0e13191fu,
    0x75869dc5u, 0x47515b67u, 0x2930373fu, 0x13181d23u, 0x738296bau, 0x49515b66u, 0x2d333a41u, 0x171c2127u,
    0x707f91b2u, 0x4a525b65u, 0x30363c43u, 0x1b20252au, 0x6f7c8dabu, 0x4b525b64u, 0x32383e44u, 0x1e23282du,
    0x6d7989a6u, 0x4c535a63u, 0x353a3f45u, 0x21262b2fu, 0x6c7786a1u, 0x4c535a62u, 0x363b4146u, 0x24282d32u,
};
float sheenAlbedoTexel(uint2 position)
{
    uint index = position.y * 16 + position.x;
    return float((sheenAlbedoTable[index >> 2] >> ((index & 3u) * 8)) & 255u) / 255;
}
float sheenAlbedo(float nv, float roughness)
{
    float2 p = saturate(float2(nv, roughness)) * 15;
    uint2 a = uint2(p), b = min(a + 1, 15);
    float2 f = frac(p);
    return lerp(lerp(sheenAlbedoTexel(a), sheenAlbedoTexel(uint2(b.x, a.y)), f.x),
                lerp(sheenAlbedoTexel(uint2(a.x, b.y)), sheenAlbedoTexel(b), f.x), f.y);
}
float3 sheenLayer(float4 sheen, float3 n, float3 v, float3 l, float3 baseBrdf)
{
#if defined(NOVA_DISABLE_SHEEN)
    return baseBrdf;
#else
    if (!any(sheen.rgb > 0))
        return baseBrdf;
    float nv = saturate(dot(n, v)), nl = saturate(dot(n, l));
    if (nv <= 0 || nl <= 0)
        return baseBrdf;
    float nvAlbedo = sheenAlbedo(nv, sheen.a), nlAlbedo = sheenAlbedo(nl, sheen.a);
    float energy = max(sheen.r, max(sheen.g, sheen.b));
    float baseWeight = 1 - energy * max(nvAlbedo, nlAlbedo);
    float invAlpha = 1 / max(.0025, sheen.a * sheen.a);
    float nh = saturate(dot(n, normalize(v + l)));
    float distribution = (2 + invAlpha) * pow(saturate(1 - nh * nh), invAlpha * .5) / 6.283185307;
    float visibility = 1 / max(4 * (nv + nl - nv * nl), 1e-6);
    return baseBrdf * baseWeight + sheen.rgb * (distribution * visibility);
#endif
}
#endif
