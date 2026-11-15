// Shared compute AO. Native RayQuery and web BVH produce identical depth/normal guides.
#include "pathtrace.hlsl"
uint2 aoSize()
{
    return asuint(parameters[24]).yz;
}
float3 aoRay(float2 pixel)
{
    float2 p = (pixel + .5) / float2(aoSize()) * 2 - 1;
    return normalize(
      parameters[12].xyz + p.x * parameters[10].w * parameters[10].xyz - p.y * parameters[11].w * parameters[11].xyz);
}
float4 aoGuide(uint2 pixel)
{
    uint i = (pixel.y * aoSize().x + pixel.x) * 4;
    return asfloat(uint4(work[i], work[i + 1], work[i + 2], work[i + 3]));
}
float3 aoPosition(uint2 pixel)
{
    return parameters[4].xyz + aoRay(float2(pixel)) * aoGuide(pixel).w;
}
uint2 aoMipSize(uint level)
{
    uint step = 1u << level;
    return (aoSize() + step - 1) / step;
}
uint aoMipOffset(uint level)
{
    uint offset = aoSize().x * aoSize().y * 4;
    for (uint l = 1; l < level; l++)
    {
        uint2 size = aoMipSize(l);
        offset += size.x * size.y;
    }
    return offset;
}
float aoDepth(uint2 pixel, uint level)
{
    if (level == 0)
    {
        float d = aoGuide(pixel).w;
        return d < 0 ? -1 : d * dot(aoRay(float2(pixel)), parameters[12].xyz);
    }
    uint2 s = aoMipSize(level);
    return asfloat(work[aoMipOffset(level) + pixel.y * s.x + pixel.x]);
}
float3 aoMipPosition(uint2 pixel, uint level)
{
    float2 full = (float2(pixel) + .5) * float(1u << level) - .5;
    float3 ray  = aoRay(full);
    return parameters[4].xyz + ray * (aoDepth(pixel, level) / max(dot(ray, parameters[12].xyz), .0001));
}
[numthreads(8, 8, 1)] void AOPrepare(uint3 id : SV_DispatchThreadID) {
    uint2 size = aoSize();
    if (any(id.xy >= size))
        return;
    Hit    hit;
    float3 ray   = aoRay(float2(id.xy));
    float4 guide = float4(0, 0, 0, -1);
    if (trace(parameters[4].xyz, ray, hit))
    {
        Vertex a = vertices[hit.primitive * 3], b = vertices[hit.primitive * 3 + 1],
               c = vertices[hit.primitive * 3 + 2];
        float3 n = normalize(cross(b.position.xyz - a.position.xyz, c.position.xyz - a.position.xyz));
        if (dot(n, ray) > 0)
            n = -n;
        guide = float4(n, hit.distance);
    }
    uint  i     = (id.y * size.x + id.x) * 4;
    uint4 bits  = asuint(guide);
    work[i]     = bits.x;
    work[i + 1] = bits.y;
    work[i + 2] = bits.z;
    work[i + 3] = bits.w;
}
  // Independent levels read the full-resolution guide, with a dispatch boundary before use.
  [numthreads(8, 8, 1)] void AOPyramid(uint3 id : SV_DispatchThreadID)
{
    for (uint level = 1; level <= 3; level++)
    {
        uint2 size = aoMipSize(level);
        if (any(id.xy >= size))
            continue;
        uint  step    = 1u << level;
        float closest = 1e20;
        for (uint y = 0; y < step; y++)
            for (uint x = 0; x < step; x++)
            {
                uint2 p = id.xy * step + uint2(x, y);
                if (any(p >= aoSize()))
                    continue;
                float d = aoDepth(p, 0);
                if (d > 0)
                    closest = min(closest, d);
            }
        work[aoMipOffset(level) + id.y * size.x + id.x] = asuint(closest == 1e20 ? -1 : closest);
    }
}
float aoSSAO(uint2 pixel, float3 position, float3 n, inout uint rng)
{
    float radius = parameters[26].x, bias = parameters[26].y;
    uint  count   = asuint(parameters[29]).w;
    float blocked = 0;
    for (uint s = 0; s < count; s++)
    {
        float3 direction = cosineDirection(n, rng);
        float  scale     = (float(s) + .5) / float(count);
        float3 q         = position + direction * radius * lerp(.1, 1, scale * scale);
        float3 delta     = q - parameters[4].xyz;
        float  z         = dot(delta, parameters[12].xyz);
        if (z <= 0)
            continue;
        float2 uv = float2(
                      dot(delta, parameters[10].xyz) / (z * parameters[10].w),
                      -dot(delta, parameters[11].xyz) / (z * parameters[11].w))
                      * .5
                    + .5;
        if (any(uv < 0) || any(uv >= 1))
            continue;
        uint2 samplePixel = min(uint2(uv * float2(aoSize())), aoSize() - 1);
        float depth       = aoDepth(samplePixel, 0);
        if (depth < 0)
            continue;
        float distance = length(aoPosition(samplePixel) - position);
        blocked += (depth < z - bias ? 1 : 0) * saturate(1 - distance / radius);
    }
    return 1 - blocked / float(count);
}
// Normal-oriented horizon integration; levels use min-depth hierarchy for MSAO.
float aoHorizon(uint2 pixel, float3 position, float3 n, uint level, inout uint rng)
{
    float radius = parameters[26].x, bias = parameters[26].y, depth = aoDepth(pixel, 0);
    uint  samples    = asuint(parameters[29]).w;
    uint  directions = min(8u, samples), steps = max(1u, samples / directions);
    float occlusion    = 0;
    float screenRadius = radius * float(aoSize().y) / (2 * parameters[11].w * max(depth, .001));
    float angleOffset  = random(rng) * 2 * PI;
    for (uint d = 0; d < directions; d++)
    {
        float  angle     = angleOffset + 2 * PI * float(d) / float(directions);
        float2 direction = float2(cos(angle), sin(angle));
        float  horizon   = 0;
        for (uint s = 1; s <= steps; s++)
        {
            float  fraction = (float(s) - .35) / float(steps);
            float  range    = level == 0 ? 1 : float(1u << level);
            float2 full     = float2(pixel) + direction * max(1, screenRadius * fraction / range) * range;
            int2   q        = int2(floor((full + .5) / float(1u << level)));
            uint2  size     = aoMipSize(level);
            if (any(q < 0) || any(q >= int2(size)))
                continue;
            if (aoDepth(uint2(q), level) < 0)
                continue;
            float3 delta    = aoMipPosition(uint2(q), level) - position;
            float  distance = length(delta);
            if (distance < bias || distance >= radius)
                continue;
            float tangent = saturate((dot(n, delta) - bias) / distance);
            horizon       = max(horizon, tangent * saturate(1 - distance * distance / (radius * radius)));
        }
        occlusion += horizon;
    }
    return saturate(1 - 2 * occlusion / float(directions));
}
float aoRTAO(float3 position, float3 n, inout uint rng)
{
    float visibility = 0;
    uint  count      = asuint(parameters[29]).w;
    for (uint i = 0; i < count; i++)
    {
        float3 d = cosineDirection(n, rng);
        Hit    hit;
        visibility += traceRay(position + n * max(parameters[26].y, .0001), d, parameters[26].x, hit) ? 0 : 1;
    }
    return visibility / float(count);
}
void aoWrite(uint2 pixel, float visibility)
{
    uint   index   = pixel.y * aoSize().x + pixel.x;
    float4 guide   = aoGuide(pixel);
    uint   display = asuint(parameters[28]).w;
    float3 color   = float3(.93, .95, .98);
    if (guide.w > 0)
    {
        float ao = parameters[26].z == 0 ? 1 : pow(max(saturate(visibility), 1e-8), parameters[26].z);
        color    = float3(.65, .65, .65) * ao + .2 * saturate(dot(guide.xyz, normalize(float3(-.4, .8, .3))));
        if (display == 1)
            color = ao.xxx;
        else if (display == 2)
            color = (1 - exp(-guide.w)).xxx;
        else if (display == 3)
            color = guide.xyz * .5 + .5;
    }
    if (asuint(parameters[25]).w & 4u)
    {
        outputImage[index] = asuint(visibility);
        return;
    }
    uint3 encoded      = uint3(saturate(pow(max(color, 0), 1 / 2.2)) * 255 + .5);
    outputImage[index] = encoded.x | (encoded.y << 8) | (encoded.z << 16) | 0xff000000u;
}
[numthreads(8, 8, 1)] void AOMain(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= aoSize()))
        return;
    uint   i          = id.y * aoSize().x + id.x;
    float4 guide      = aoGuide(id.xy);
    uint4  frame      = asuint(parameters[24]);
    uint   method     = asuint(parameters[26]).w;
    uint   rng        = mltHash(i + asuint(parameters[30]).w * 7919u + (method == 4 ? frame.x * 9277u : 0));
    float  visibility = 1;
    if (guide.w > 0)
    {
        float3 position = aoPosition(id.xy);
        if (method == 1)
            visibility = aoSSAO(id.xy, position, guide.xyz, rng);
        else if (method == 2)
            visibility = aoHorizon(id.xy, position, guide.xyz, 0, rng);
        else if (method == 3)
        {
            visibility = 0;
            for (uint l = 0; l < 4; l++)
                visibility += aoHorizon(id.xy, position, guide.xyz, l, rng) * .25;
        }
        else if (method == 4)
            visibility = aoRTAO(position, guide.xyz, rng);
    }
    float sum = visibility;
    if (frame.x > 0 && method == 4)
        sum += accumulation[i].x;
    else
        sum *= float(frame.x + 1);
    accumulation[i] = float4(sum, 0, 0, 1);
    aoWrite(id.xy, sum / float(frame.x + 1));
}[numthreads(8, 8, 1)] void AOFilter(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= aoSize()))
        return;
    float4 center = aoGuide(id.xy);
    if (center.w < 0)
        return;
    float sum = 0, total = 0;
    for (int y = -2; y <= 2; y++)
        for (int x = -2; x <= 2; x++)
        {
            int2 p = int2(id.xy) + int2(x, y);
            if (any(p < 0) || any(p >= int2(aoSize())))
                continue;
            float4 g = aoGuide(uint2(p));
            if (g.w < 0)
                continue;
            float weight = exp(-float(x * x + y * y) / 4) * pow(saturate(dot(center.xyz, g.xyz)), 32)
                           * exp(-abs(center.w - g.w) / max(.002, center.w * .01));
            sum += accumulation[uint(p.y) * aoSize().x + uint(p.x)].x * weight;
            total += weight;
        }
    aoWrite(id.xy, sum / max(total, 1e-8) / float(asuint(parameters[24]).x + 1));
}
