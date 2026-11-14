struct Vertex
{
    float4 position;
    float4 normal;
    float4 uv;
};
#ifdef NOVA_WEB_BVH
struct BVHNode
{
    float3 minimum;
    uint   escape;
    float3 maximum;
    uint   first;
    uint4  metadata;
};
[[vk::binding(0, 0)]] StructuredBuffer<BVHNode> nodes : register(t0);
#else
[[vk::binding(0, 0)]] RaytracingAccelerationStructure scene : register(t0);
#endif
[[vk::binding(1, 0)]] StructuredBuffer<float4>   parameters : register(t1);
[[vk::binding(2, 0)]] StructuredBuffer<Vertex>   vertices : register(t2);
[[vk::binding(3, 0)]] StructuredBuffer<uint>     texels : register(t3);
[[vk::binding(4, 0)]] StructuredBuffer<uint2>    skyTexels : register(t4);
[[vk::binding(5, 0)]] RWStructuredBuffer<float4> accumulation : register(u0);
[[vk::binding(6, 0)]] RWStructuredBuffer<uint>   outputImage : register(u1);
[[vk::binding(7, 0)]] RWStructuredBuffer<float4> guides : register(u2);
static const float                               PI = 3.14159265;
uint                                             randomBits(inout uint state)
{
    state     = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
float random(inout uint state)
{
    return (randomBits(state) >> 8) * 0.000000059604644775390625;
}
float4 unpackColor(uint value)
{
    return float4(value & 255, (value >> 8) & 255, (value >> 16) & 255, (value >> 24) & 255) / 255.0;
}
float4 imagePixel(uint4 image, int2 p)
{
    int2 n = int2(image.yz);
    p      = (p % n + n) % n;
    return unpackColor(texels[image.x + uint(p.y) * image.y + uint(p.x)]);
}
float4 textureColor(uint index, float2 uv)
{
    uint4  image = asuint(parameters[27 + index]);
    float2 p     = frac(uv) * float2(image.yz) - 0.5;
    int2   base  = int2(floor(p));
    float2 f     = frac(p);
    return lerp(
      lerp(imagePixel(image, base), imagePixel(image, base + int2(1, 0)), f.x),
      lerp(imagePixel(image, base + int2(0, 1)), imagePixel(image, base + 1), f.x),
      f.y);
}
float3 skyPixel(uint face, int2 p, uint size)
{
    p       = clamp(p, int2(0, 0), int2(size - 1, size - 1));
    uint2 h = skyTexels[(face * size + uint(p.y)) * size + uint(p.x)];
    return float3(f16tof32(h.x & 65535), f16tof32(h.x >> 16), f16tof32(h.y & 65535));
}
float3 sky(float3 direction)
{
    float4 rotation = parameters[13];
    float3 d        = float3(
      rotation.y * direction.x + rotation.x * direction.z,
      direction.y,
      -rotation.x * direction.x + rotation.y * direction.z);
    if (parameters[12].w < 0.5)
        return lerp(float3(0.025, 0.03, 0.04), float3(0.45, 0.52, 0.65), saturate(d.y * .5 + .5));
    float3 a = abs(d);
    uint   face;
    float2 uv;
    if (a.x >= a.y && a.x >= a.z)
    {
        face = d.x > 0 ? 0 : 1;
        uv   = float2(d.x > 0 ? -d.z : d.z, -d.y) / a.x;
    }
    else if (a.y >= a.z)
    {
        face = d.y > 0 ? 2 : 3;
        uv   = float2(d.x, d.y > 0 ? d.z : -d.z) / a.y;
    }
    else
    {
        face = d.z > 0 ? 4 : 5;
        uv   = float2(d.z > 0 ? d.x : -d.x, -d.y) / a.z;
    }
    uint   size = asuint(parameters[25]).y;
    float2 p    = (uv * .5 + .5) * size - .5;
    int2   b    = int2(floor(p));
    float2 f    = frac(p);
    return lerp(
      lerp(skyPixel(face, b, size), skyPixel(face, b + int2(1, 0), size), f.x),
      lerp(skyPixel(face, b + int2(0, 1), size), skyPixel(face, b + 1, size), f.x),
      f.y);
}
struct Hit
{
    float  distance;
    uint   primitive;
    float2 bary;
};
bool trace(float3 origin, float3 direction, out Hit hit)
{
#ifdef NOVA_WEB_BVH
    hit.distance  = 10000;
    hit.primitive = 0;
    hit.bary      = 0;
    bool found    = false;
    uint index    = 0;
    uint count    = asuint(parameters[25]).z;
    while (index < count)
    {
        BVHNode node = nodes[index];
        // Parallel slabs need an explicit branch: 0 * infinity would be NaN.
        float nearT = .0005, farT = hit.distance;
        bool  intersects = true;
        for (uint axis = 0; axis < 3; ++axis)
        {
            if (abs(direction[axis]) < 1e-20)
                intersects = intersects && origin[axis] >= node.minimum[axis] && origin[axis] <= node.maximum[axis];
            else
            {
                float a = (node.minimum[axis] - origin[axis]) / direction[axis];
                float b = (node.maximum[axis] - origin[axis]) / direction[axis];
                nearT   = max(nearT, min(a, b));
                farT    = min(farT, max(a, b));
            }
        }
        if (!intersects || nearT > farT)
        {
            index = node.escape;
            continue;
        }
        if (node.metadata.x == 0)
        {
            ++index;
            continue;
        }
        for (uint i = 0; i < node.metadata.x; ++i)
        {
            uint   primitive = node.first + i;
            float3 a         = vertices[primitive * 3].position.xyz;
            float3 e1        = vertices[primitive * 3 + 1].position.xyz - a;
            float3 e2        = vertices[primitive * 3 + 2].position.xyz - a;
            float3 p         = cross(direction, e2);
            float  det       = dot(e1, p);
            if (abs(det) < 1e-10)
                continue;
            float3 t        = origin - a;
            float  u        = dot(t, p) / det;
            float3 q        = cross(t, e1);
            float  v        = dot(direction, q) / det;
            float  distance = dot(e2, q) / det;
            if (u >= 0 && v >= 0 && u + v <= 1 && distance >= .0005 && distance < hit.distance)
            {
                hit.distance  = distance;
                hit.primitive = primitive;
                hit.bary      = float2(u, v);
                found         = true;
            }
        }
        index = node.escape;
    }
    return found;
#else
    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = direction;
    ray.TMin      = .0005;
    ray.TMax      = 10000;
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(scene, RAY_FLAG_NONE, 255, ray);
    while (query.Proceed())
    {
    }
    bool found    = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    hit.distance  = found ? query.CommittedRayT() : 0;
    hit.primitive = found ? query.CommittedPrimitiveIndex() : 0;
    hit.bary      = found ? query.CommittedTriangleBarycentrics() : float2(0, 0);
    return found;
#endif
}
float3 fresnel(float cosine, float3 f0)
{
    return f0 + (1 - f0) * pow(1 - saturate(cosine), 5);
}
float distribution(float nh, float roughness)
{
    float a = roughness * roughness, a2 = a * a, q = nh * nh * (a2 - 1) + 1;
    return a2 / max(PI * q * q, 1e-7);
}
float3 brdf(float3 n, float3 v, float3 l, float3 base, float metal, float roughness)
{
    float  nv = max(dot(n, v), 1e-5), nl = max(dot(n, l), 0);
    float3 h  = normalize(v + l);
    float  nh = saturate(dot(n, h));
    float  k  = pow(roughness + 1, 2) / 8;
    float  g  = nv / (nv * (1 - k) + k) * nl / max(nl * (1 - k) + k, 1e-5);
    float3 f  = fresnel(dot(v, h), lerp(float3(.04, .04, .04), base, metal));
    return (1 - f) * (1 - metal) * base / PI + distribution(nh, roughness) * g * f / max(4 * nv * nl, 1e-5);
}
float3 path(float3 origin, float3 direction, inout uint state, uint bounces)
{
    float3 sum = 0, weight = 1;
    for (uint depth = 0; depth <= bounces; ++depth)
    {
        Hit hit;
        if (!trace(origin, direction, hit))
        {
            if (depth == 0 && parameters[14].y < .5)
                sum += weight * parameters[26].rgb;
            else
                sum += weight * sky(direction) * (depth == 0 ? parameters[14].x : parameters[13].z);
            break;
        }
        Vertex a = vertices[hit.primitive * 3], b = vertices[hit.primitive * 3 + 1],
               c       = vertices[hit.primitive * 3 + 2];
        float3 weights = float3(1 - hit.bary.x - hit.bary.y, hit.bary);
        float2 uv      = a.uv.xy * weights.x + b.uv.xy * weights.y + c.uv.xy * weights.z;
        float3 base    = pow(max(textureColor(0, uv).rgb, 0), 2.2) * parameters[5].rgb;
        sum += weight * pow(max(textureColor(4, uv).rgb, 0), 2.2) * parameters[7].rgb;
        if (depth == bounces)
            break;
        float3 v = -direction, edge1 = b.position.xyz - a.position.xyz, edge2 = c.position.xyz - a.position.xyz;
        float3 geometric = normalize(cross(edge1, edge2));
        if (dot(geometric, v) < 0)
            geometric = -geometric;
        float3 n = normalize(a.normal.xyz * weights.x + b.normal.xyz * weights.y + c.normal.xyz * weights.z);
        if (dot(n, v) < 0)
            n = -n;
        float2 du = b.uv.xy - a.uv.xy, dv = c.uv.xy - a.uv.xy;
        float  det = du.x * dv.y - du.y * dv.x;
        if (abs(det) > 1e-8)
        {
            float3 t = (edge1 * dv.y - edge2 * du.y) / det, bit = (-edge1 * dv.x + edge2 * du.x) / det;
            t             = normalize(t - n * dot(n, t));
            bit           = normalize(bit - n * dot(n, bit));
            float3 normal = textureColor(1, uv).xyz * 2 - 1;
            normal.xy *= parameters[6].z;
            n = normalize(t * normal.x + bit * normal.y + n * normal.z);
        }
        float4 mr    = textureColor(2, uv);
        float  metal = saturate(mr.b * parameters[6].x), roughness = clamp(mr.g * parameters[6].y, .06, 1);
        float3 position = origin + direction * hit.distance, light = parameters[8].xyz;
        if (parameters[9].w > 0 && dot(n, light) > 0)
        {
            Hit shadow;
            if (!trace(position + geometric * .001, light, shadow))
                sum += weight * brdf(n, v, light, base, metal, roughness) * max(dot(n, light), 0) * parameters[9].rgb
                       * parameters[9].w;
        }
        float3 tangent     = normalize(cross(abs(n.y) < .999 ? float3(0, 1, 0) : float3(1, 0, 0), n)),
               bitangent   = cross(n, tangent);
        float  probability = lerp(.25, .9, metal), u = random(state), q = random(state), phi = 2 * PI * random(state);
        float3 l;
        if (u < probability)
        {
            float alpha = roughness * roughness, cosine = sqrt((1 - q) / (1 + (alpha * alpha - 1) * q)),
                  sine = sqrt(max(0, 1 - cosine * cosine));
            float3 h   = tangent * (cos(phi) * sine) + bitangent * (sin(phi) * sine) + n * cosine;
            l          = reflect(-v, h);
        }
        else
        {
            float radius = sqrt(q);
            l            = tangent * (cos(phi) * radius) + bitangent * (sin(phi) * radius) + n * sqrt(1 - q);
        }
        float nl = dot(n, l);
        if (nl <= 0 || dot(geometric, l) <= 0)
            break;
        float3 halfVector = normalize(v + l);
        float  pdf        = (1 - probability) * nl / PI
                    + probability * distribution(saturate(dot(n, halfVector)), roughness) * saturate(dot(n, halfVector))
                        / max(4 * abs(dot(v, halfVector)), 1e-6);
        weight *= brdf(n, v, l, base, metal, roughness) * nl / max(pdf, 1e-6);
        if (depth >= 2)
        {
            float survive = clamp(max(weight.x, max(weight.y, weight.z)), .1, .95);
            if (random(state) > survive)
                break;
            weight /= survive;
        }
        direction = l;
        origin    = position + geometric * .001;
    }
    return min(max(sum, 0), float3(64, 64, 64));
}
[numthreads(8, 8, 1)] void CSMain(uint3 id : SV_DispatchThreadID) {
    uint4 frame = asuint(parameters[24]), options = asuint(parameters[25]);
    if (id.x >= frame.y || id.y >= frame.z)
        return;
    uint index = id.y * frame.y + id.x;
    if (options.w != 0)
    {
        float2 p         = (float2(id.xy) + .5) / float2(frame.yz) * 2 - 1;
        float3 direction = normalize(
          parameters[12].xyz + p.x * parameters[10].w * parameters[10].xyz
          - p.y * parameters[11].w * parameters[11].xyz);
        Hit    hit;
        float4 guide = float4(0, 0, 0, -1);
        if (trace(parameters[4].xyz, direction, hit))
        {
            Vertex a = vertices[hit.primitive * 3], b = vertices[hit.primitive * 3 + 1],
                   c       = vertices[hit.primitive * 3 + 2];
            float3 weights = float3(1 - hit.bary.x - hit.bary.y, hit.bary);
            guide          = float4(
              normalize(a.normal.xyz * weights.x + b.normal.xyz * weights.y + c.normal.xyz * weights.z), hit.distance);
        }
        guides[index] = guide;
    }
    float3 sum = frame.x ? accumulation[index].rgb : float3(0, 0, 0);
    for (uint sampleIndex = 0; sampleIndex < options.x; ++sampleIndex)
    {
        uint state = index * 1973u + (frame.x + sampleIndex) * 9277u + 89173u;
        randomBits(state);
        float2 p         = (float2(id.xy) + float2(random(state), random(state))) / float2(frame.yz) * 2 - 1;
        float3 direction = normalize(
          parameters[12].xyz + p.x * parameters[10].w * parameters[10].xyz
          - p.y * parameters[11].w * parameters[11].xyz);
        sum += path(parameters[4].xyz, direction, state, frame.w);
    }
    accumulation[index] = float4(sum, 1);
    float3 color        = 1 - exp(-sum / float(frame.x + options.x) * parameters[4].w);
    uint3  encoded      = uint3(saturate(pow(max(color, 0), 1 / 2.2)) * 255 + .5);
    outputImage[index]  = encoded.x | (encoded.y << 8) | (encoded.z << 16) | 0xff000000u;
}

  // Display-only joint bilateral filter. Raw progressive radiance is never modified.
  // Geometry guides stop filtering across silhouettes, depth breaks and normal edges.
  [numthreads(8, 8, 1)] void DenoiseMain(uint3 id : SV_DispatchThreadID)
{
    uint4 frame = asuint(parameters[24]), options = asuint(parameters[25]);
    if (id.x >= frame.y || id.y >= frame.z)
        return;
    uint   index       = id.y * frame.y + id.x;
    float4 centerGuide = guides[index];
    if (centerGuide.w < 0)
        return;
    float  count      = float(frame.x + options.x);
    float3 center     = accumulation[index].rgb / count;
    float  centerLuma = dot(center, float3(.2126, .7152, .0722));
    float3 sum        = 0;
    float  total      = 0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
        {
            int2 pixel = int2(id.xy) + int2(x, y);
            if (any(pixel < 0) || pixel.x >= int(frame.y) || pixel.y >= int(frame.z))
                continue;
            uint   neighbor = uint(pixel.y) * frame.y + uint(pixel.x);
            float4 guide    = guides[neighbor];
            if (guide.w < 0)
                continue;
            float3 value  = accumulation[neighbor].rgb / count;
            float  luma   = dot(value, float3(.2126, .7152, .0722));
            float  weight = exp(-float(x * x + y * y) / 4.0) * pow(saturate(dot(centerGuide.xyz, guide.xyz)), 32)
                           * exp(-abs(guide.w - centerGuide.w) / max(.005, centerGuide.w * .015))
                           * exp(-abs(luma - centerLuma) / max(.15, centerLuma * .75));
            sum += value * weight;
            total += weight;
        }
    float3 color       = 1 - exp(-sum / max(total, 1e-6) * parameters[4].w);
    uint3  encoded     = uint3(saturate(pow(max(color, 0), 1 / 2.2)) * 255 + .5);
    outputImage[index] = encoded.x | (encoded.y << 8) | (encoded.z << 16) | 0xff000000u;
}
