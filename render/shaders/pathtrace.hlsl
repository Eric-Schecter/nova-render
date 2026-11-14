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
[[vk::binding(7, 0)]] RWStructuredBuffer<uint>   work : register(u2);
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
#if !defined(NOVA_WEB_BVH) && !defined(NOVA_DENOISE_ONLY)
// All traversals finish before the next begins. A shader-private query avoids
// reserving a traversal object for every inlined eye/light/shadow call site.
static RayQuery<RAY_FLAG_NONE> query;
#endif
bool traceRay(float3 origin, float3 direction, float maxDistance, out Hit hit)
{
#ifdef NOVA_WEB_BVH
    hit.distance  = maxDistance;
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
    ray.TMax      = maxDistance;
#ifdef NOVA_DENOISE_ONLY
    // Keep the unused traversal helper parseable without a shader-wide query
    // initializer; older DXIL validators otherwise retain a stale DXR flag.
    RayQuery<RAY_FLAG_NONE> query;
#endif
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
bool trace(float3 origin, float3 direction, out Hit hit)
{
    return traceRay(origin, direction, 10000, hit);
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
// Power heuristic for one sample from each strategy; scale first to avoid overflow.
float misWeight(float a, float b)
{
    float scale = max(a, b);
    if (scale <= 0)
        return 1;
    a /= scale;
    b /= scale;
    return a * a / (a * a + b * b);
}
uint lightHeader()
{
    return asuint(parameters[27]).w;
}
float areaProbability()
{
    uint  h = lightHeader();
    float a = asfloat(texels[h + 5]), e = asfloat(texels[h + 6]) * max(parameters[13].z, 0);
    if (a <= 0)
        return 0;
    if (e <= 0 || texels[h + 4] == 0)
        return 1;
    return clamp(a / (a + e), .05, .95);
}
uint selectCDF(uint offset, uint count, uint stride, float u)
{
    uint lo = 0, hi = count;
    while (lo < hi)
    {
        uint mid = lo + (hi - lo) / 2;
        if (asfloat(texels[offset + mid * stride]) <= u)
            lo = mid + 1;
        else
            hi = mid;
    }
    return min(lo, count - 1);
}
void cubeCoordinates(float3 d, out uint face, out float2 uv)
{
    float3 a = abs(d);
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
}
float3 cubeDirection(uint face, float2 uv)
{
    if (face == 0)
        return normalize(float3(1, -uv.y, -uv.x));
    if (face == 1)
        return normalize(float3(-1, -uv.y, uv.x));
    if (face == 2)
        return normalize(float3(uv.x, 1, uv.y));
    if (face == 3)
        return normalize(float3(uv.x, -1, -uv.y));
    if (face == 4)
        return normalize(float3(uv.x, -uv.y, 1));
    return normalize(float3(-uv.x, -uv.y, -1));
}
float environmentPdf(float3 direction)
{
    uint h = lightHeader(), grid = texels[h + 4];
    if (grid == 0 || parameters[13].z <= 0)
        return 0;
    float2 rotation = parameters[13].xy;
    float3 d        = float3(
      rotation.y * direction.x + rotation.x * direction.z,
      direction.y,
      -rotation.x * direction.x + rotation.y * direction.z);
    uint   face;
    float2 uv;
    cubeCoordinates(d, face, uv);
    uint2 p = min(uint2(saturate(uv * .5 + .5) * grid), grid - 1);
    uint  i = (face * grid + p.y) * grid + p.x, offset = texels[h + 3];
    float pmf = asfloat(texels[offset + i]) - (i ? asfloat(texels[offset + i - 1]) : 0);
    return (1 - areaProbability()) * pmf * grid * grid * .25 * pow(1 + dot(uv, uv), 1.5);
}
float emitterPdf(uint primitive, float3 origin, float3 position)
{
    uint  h   = lightHeader();
    float pmf = asfloat(texels[texels[h + 2] + primitive]);
    if (pmf <= 0)
        return 0;
    float3 a = vertices[primitive * 3].position.xyz, b = vertices[primitive * 3 + 1].position.xyz,
           c            = vertices[primitive * 3 + 2].position.xyz;
    float3 crossProduct = cross(b - a, c - a), delta = position - origin;
    float  area = .5 * length(crossProduct), distance2 = dot(delta, delta);
    float  cosine = abs(dot(normalize(crossProduct), -normalize(delta)));
    return cosine > 1e-8 ? areaProbability() * pmf * distance2 / (area * cosine) : 0;
}
float bsdfPdf(float3 n, float3 v, float3 l, float metal, float roughness)
{
    float nl = dot(n, l);
    if (nl <= 0)
        return 0;
    float  probability = lerp(.25, .9, metal);
    float3 h           = normalize(v + l);
    float  nh          = saturate(dot(n, h));
    return (1 - probability) * nl / PI
           + probability * distribution(nh, roughness) * nh / max(4 * abs(dot(v, h)), 1e-20);
}
bool sampleLight(
  float3     origin,
  inout uint state,
  out float3 direction,
  out float3 radiance,
  out float  pdf,
  out float  maxDistance,
  bool       emitters = true)
{
    uint  h     = lightHeader();
    float pArea = areaProbability();
    direction   = 0;
    radiance    = 0;
    pdf         = 0;
    maxDistance = 10000;
    if (random(state) < pArea)
    {
        uint   record    = texels[h + 1] + 4 * selectCDF(texels[h + 1], texels[h], 4, random(state));
        uint   primitive = texels[record + 2];
        Vertex a = vertices[primitive * 3], b = vertices[primitive * 3 + 1], c = vertices[primitive * 3 + 2];
        float  r = sqrt(random(state)), s = random(state);
        float3 bary     = float3(1 - r, r * (1 - s), r * s);
        float3 position = a.position.xyz * bary.x + b.position.xyz * bary.y + c.position.xyz * bary.z;
        float2 uv       = a.uv.xy * bary.x + b.uv.xy * bary.y + c.uv.xy * bary.z;
        float3 delta    = position - origin;
        float  distance = length(delta);
        if (distance <= .002)
            return false;
        direction   = delta / distance;
        maxDistance = distance - .001;
        pdf         = emitterPdf(primitive, origin, position);
        radiance    = emitters ? pow(max(textureColor(4, uv).rgb, 0), 2.2) * parameters[7].rgb : float3(0, 0, 0);
    }
    else
    {
        uint grid = texels[h + 4];
        if (grid == 0 || parameters[13].z <= 0)
            return false;
        uint   i    = selectCDF(texels[h + 3], 6 * grid * grid, 1, random(state));
        uint   face = i / (grid * grid), cell = i % (grid * grid);
        float2 uv       = (float2(cell % grid, cell / grid) + float2(random(state), random(state))) / grid * 2 - 1;
        float3 d        = cubeDirection(face, uv);
        float2 rotation = parameters[13].xy;
        direction       = float3(rotation.y * d.x - rotation.x * d.z, d.y, rotation.x * d.x + rotation.y * d.z);
        pdf             = environmentPdf(direction);
        radiance        = sky(direction) * parameters[13].z;
    }
    return pdf > 0;
}
// Finite-area-light BDPT, camera-conditioned strategies only (no camera splats).
// Densities share surface-area measure; the fixed camera-ray density cancels.
struct PathVertex
{
    float3 position;
    float3 geometric;
    float3 normal;
    float3 base;
    float3 emission;
    float3 beta;
    float  metal;
    float  roughness;
    uint   primitive;
};
// Thread-private storage shared by the sequential subpath/MIS helpers. Passing
// whole path arrays by value would duplicate them at every connection.
static PathVertex eyeVertices[9];
static PathVertex lightVertices[9];
PathVertex        pathVertex(Hit hit, float3 origin, float3 direction)
{
    Vertex a = vertices[hit.primitive * 3], b = vertices[hit.primitive * 3 + 1], c = vertices[hit.primitive * 3 + 2];
    float3 bary = float3(1 - hit.bary.x - hit.bary.y, hit.bary);
    float2 uv   = a.uv.xy * bary.x + b.uv.xy * bary.y + c.uv.xy * bary.z;
    float3 e = b.position.xyz - a.position.xyz, f = c.position.xyz - a.position.xyz;
    PathVertex p;
    p.position  = origin + direction * hit.distance;
    p.geometric = normalize(cross(e, f));
    p.normal    = normalize(a.normal.xyz * bary.x + b.normal.xyz * bary.y + c.normal.xyz * bary.z);
    float2 du = b.uv.xy - a.uv.xy, dv = c.uv.xy - a.uv.xy;
    float  det = du.x * dv.y - du.y * dv.x;
    if (abs(det) > 1e-8)
    {
        float3 t = (e * dv.y - f * du.y) / det, bit = (-e * dv.x + f * du.x) / det;
        t             = normalize(t - p.normal * dot(p.normal, t));
        bit           = normalize(bit - p.normal * dot(p.normal, bit));
        float3 mapped = textureColor(1, uv).xyz * 2 - 1;
        mapped.xy *= parameters[6].z;
        p.normal = normalize(t * mapped.x + bit * mapped.y + p.normal * mapped.z);
    }
    p.base      = pow(max(textureColor(0, uv).rgb, 0), 2.2) * parameters[5].rgb;
    p.emission  = pow(max(textureColor(4, uv).rgb, 0), 2.2) * parameters[7].rgb;
    float4 mr   = textureColor(2, uv);
    p.metal     = saturate(mr.b * parameters[6].x);
    p.roughness = clamp(mr.g * parameters[6].y, .06, 1);
    p.beta      = 1;
    p.primitive = hit.primitive;
    return p;
}
void pathFrame(PathVertex p, float3 v, out float3 n, out float3 g)
{
    n = dot(p.normal, v) < 0 ? -p.normal : p.normal;
    g = dot(p.geometric, v) < 0 ? -p.geometric : p.geometric;
    if (dot(n, v) <= 1e-6)
        n = g;
}
float3 path(float3 origin, float3 direction, inout uint state, uint bounces, bool emitters = true)
{
    float3 sum = 0, weight = 1;
    float  previousPdf = 0;
    bool   useMIS      = !emitters || (asuint(parameters[25]).w & 2u) != 0;
    for (uint depth = 0; depth <= bounces; ++depth)
    {
        Hit hit;
        if (!trace(origin, direction, hit))
        {
            if (depth == 0 && parameters[14].y < .5)
                sum += weight * parameters[26].rgb;
            else
                sum += weight * sky(direction) * (depth == 0 ? parameters[14].x : parameters[13].z)
                       * (useMIS && depth > 0 ? misWeight(previousPdf, environmentPdf(direction)) : 1);
            break;
        }
        PathVertex surface = pathVertex(hit, origin, direction);
        float3     base    = surface.base;
        sum +=
          (emitters ? 1 : 0) * weight * surface.emission
          * (useMIS && depth > 0 ? misWeight(previousPdf, emitterPdf(hit.primitive, origin, surface.position)) : 1);
        if (depth == bounces || (!emitters && parameters[13].z <= 0 && parameters[9].w <= 0))
            break;
        float3 v = -direction, n, geometric;
        pathFrame(surface, v, n, geometric);
        float  metal = surface.metal, roughness = surface.roughness;
        float3 position = origin + direction * hit.distance, light = parameters[8].xyz;
        if (parameters[9].w > 0 && dot(n, light) > 0 && dot(geometric, light) > 0)
        {
            Hit shadow;
            if (!trace(position + geometric * .001, light, shadow))
                sum += weight * brdf(n, v, light, base, metal, roughness) * max(dot(n, light), 0) * parameters[9].rgb
                       * parameters[9].w;
        }
        if (useMIS)
        {
            float3 l, radiance;
            float  pdf, maxDistance;
            float3 nextOrigin = position + geometric * .001;
            if (
              sampleLight(nextOrigin, state, l, radiance, pdf, maxDistance, emitters) && dot(n, l) > 0
              && dot(geometric, l) > 0)
            {
                Hit shadow;
                if (!traceRay(nextOrigin, l, maxDistance, shadow))
                    sum += weight * brdf(n, v, l, base, metal, roughness) * dot(n, l) * radiance / pdf
                           * misWeight(pdf, bsdfPdf(n, v, l, metal, roughness));
            }
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
        float pdf = bsdfPdf(n, v, l, metal, roughness);
        if (pdf <= 0)
            break;
        previousPdf = pdf;
        weight *= brdf(n, v, l, base, metal, roughness) * nl / pdf;
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
// Radiance BSDF expressed against geometric projected area. Reversing v/l for
// light walking supplies the adjoint shading-normal correction, without assuming
// a shading normal equals the geometric normal.
float3 pathF(PathVertex p, float3 v, float3 l)
{
    float3 n, g;
    pathFrame(p, v, n, g);
    float cosine = dot(g, l);
    if (cosine <= 1e-7 || dot(n, l) <= 0)
        return 0;
    return brdf(n, v, l, p.base, p.metal, p.roughness) * dot(n, l) / cosine;
}
float pathPdf(PathVertex p, float3 v, float3 l)
{
    float3 n, g;
    pathFrame(p, v, n, g);
    return dot(g, l) > 0 ? bsdfPdf(n, v, l, p.metal, p.roughness) : 0;
}
float3 cosineDirection(float3 n, inout uint state)
{
    float3 t = normalize(cross(abs(n.y) < .999 ? float3(0, 1, 0) : float3(1, 0, 0), n)), b = cross(n, t);
    float  q = random(state), phi = 2 * PI * random(state);
    return t * (cos(phi) * sqrt(q)) + b * (sin(phi) * sqrt(q)) + n * sqrt(1 - q);
}
bool walkDirection(PathVertex p, float3 v, inout uint state, out float3 l, out float pdf)
{
    float3 n, g;
    pathFrame(p, v, n, g);
    float  u = random(state), q = random(state), phi = 2 * PI * random(state);
    float3 t = normalize(cross(abs(n.y) < .999 ? float3(0, 1, 0) : float3(1, 0, 0), n)), b = cross(n, t);
    if (u < lerp(.25, .9, p.metal))
    {
        float  alpha = p.roughness * p.roughness, c = sqrt((1 - q) / (1 + (alpha * alpha - 1) * q));
        float3 h = t * (cos(phi) * sqrt(max(0, 1 - c * c))) + b * (sin(phi) * sqrt(max(0, 1 - c * c))) + n * c;
        l        = reflect(-v, h);
    }
    else
        l = t * (cos(phi) * sqrt(q)) + b * (sin(phi) * sqrt(q)) + n * sqrt(1 - q);
    pdf = pathPdf(p, v, l);
    return pdf > 0;
}
float3 pathOrigin(PathVertex p, float3 direction)
{
    return p.position + p.geometric * (dot(p.geometric, direction) < 0 ? -.0006 : .0006);
}
float sourcePdf(PathVertex p)
{
    float3 a = vertices[p.primitive * 3].position.xyz, b = vertices[p.primitive * 3 + 1].position.xyz,
           c   = vertices[p.primitive * 3 + 2].position.xyz;
    float area = .5 * length(cross(b - a, c - a));
    return area > 0 ? asfloat(texels[texels[lightHeader() + 2] + p.primitive]) / area : 0;
}
PathVertex pathPoint(uint eyeCount, uint lightCount, uint index)
{
    if (index < eyeCount)
        return eyeVertices[min(index, 8u)];
    return lightVertices[min(lightCount - 1 - (index - eyeCount), 8u)];
}
float connectionWeight(uint eyeCount, uint lightCount)
{
    uint count = eyeCount + lightCount, selected = eyeCount;
    if (count == 1)
        return 1;
    PathVertex endpoint  = pathPoint(eyeCount, lightCount, count - 1);
    float      pdfSource = sourcePdf(endpoint);
    if (pdfSource <= 0)
        return lightCount == 0 ? 1 : 0;
    // Adjacent strategy density ratios: p_k/p_(k+1). The common camera-ray
    // density and all unchanged path edges cancel. Stream log-sum-exp so no
    // duplicate full-path array or per-strategy scratch arrays are needed.
    float current = 0, maximum = 0, total = 1, chosen = selected == count ? 0 : -1e30;
    for (int k = int(count) - 1; k >= 1; --k)
    {
        PathVertex target   = pathPoint(eyeCount, lightCount, uint(k));
        PathVertex fromEye  = pathPoint(eyeCount, lightCount, uint(k - 1));
        float3     previous = parameters[4].xyz;
        if (k > 1)
            previous = pathPoint(eyeCount, lightCount, uint(k - 2)).position;
        float3 delta   = target.position - fromEye.position;
        float3 d       = normalize(delta);
        float  forward = pathPdf(fromEye, normalize(previous - fromEye.position), d) * abs(dot(target.geometric, -d))
                        / max(dot(delta, delta), 1e-20);
        float reverse = pdfSource;
        if (k < int(count) - 1)
        {
            PathVertex fromLight = pathPoint(eyeCount, lightCount, uint(k + 1));
            delta                = target.position - fromLight.position;
            d                    = normalize(delta);
            float pdf;
            if (k + 1 == int(count) - 1)
                pdf = abs(dot(fromLight.geometric, d)) / (2 * PI);
            else
            {
                float3 next = pathPoint(eyeCount, lightCount, uint(k + 2)).position;
                pdf         = pathPdf(fromLight, normalize(next - fromLight.position), d);
            }
            reverse = pdf * abs(dot(target.geometric, -d)) / max(dot(delta, delta), 1e-20);
        }
        // Non-delta reflective BSDFs have positive sampling density wherever
        // their contribution is nonzero. A floor handles underflow at grazing edges.
        current += log2(max(reverse, 1e-30)) - log2(max(forward, 1e-30));
        float logWeight = 2 * current;
        if (logWeight > maximum)
        {
            total   = total * exp2(maximum - logWeight) + 1;
            maximum = logWeight;
        }
        else
            total += exp2(logWeight - maximum);
        if (uint(k) == selected)
            chosen = logWeight;
    }
    return exp2(chosen - maximum) / total;
}
float3 bidirectional(float3 origin, float3 direction, inout uint state, uint bounces)
{
    // This estimator contains finite emitters only; CSMain separately evaluates
    // environment/delta lights with the shared camera-path estimator.
    uint   header   = lightHeader();
    float3 result   = 0;
    uint   eyeCount = 0, lightCount = 0;
    float3 beta = 1, rayOrigin = origin, rayDirection = direction;
    // No roulette: every strategy of a bounded path length has one sample.
    for (uint depth = 0; depth <= bounces; ++depth)
    {
        Hit hit;
        if (!trace(rayOrigin, rayDirection, hit))
            break;
        PathVertex p            = pathVertex(hit, rayOrigin, rayDirection);
        p.beta                  = beta;
        eyeVertices[eyeCount++] = p;
        if (depth == bounces)
            break;
        float3 l;
        float  pdf;
        if (!walkDirection(p, -rayDirection, state, l, pdf))
            break;
        beta *= pathF(p, -rayDirection, l) * abs(dot(p.geometric, l)) / pdf;
        rayOrigin    = pathOrigin(p, l);
        rayDirection = l;
    }
    // The source position is independent of the eye path. Its area PMF is the
    // same table as PT NEE, but is not multiplied by the environment mixture.
    uint   record    = texels[header + 1] + 4 * selectCDF(texels[header + 1], texels[header], 4, random(state));
    uint   primitive = texels[record + 2];
    float  r = sqrt(random(state)), s = random(state);
    float3 bary = float3(1 - r, r * (1 - s), r * s);
    Hit    source;
    source.primitive = primitive;
    source.bary      = bary.yz;
    source.distance  = 0;
    float3 position  = vertices[primitive * 3].position.xyz * bary.x + vertices[primitive * 3 + 1].position.xyz * bary.y
                      + vertices[primitive * 3 + 2].position.xyz * bary.z;
    PathVertex emitter = pathVertex(source, position, float3(0, 0, 1));
    float      pdfA    = sourcePdf(emitter);
    if (pdfA > 0 && any(emitter.emission > 0))
    {
        emitter.beta                = emitter.emission / pdfA;
        lightVertices[lightCount++] = emitter;
        float3 n                    = random(state) < .5 ? emitter.geometric : -emitter.geometric;
        rayDirection                = cosineDirection(n, state);
        rayOrigin                   = pathOrigin(emitter, rayDirection);
        beta                        = emitter.beta * (2 * PI);
        for (uint depth = 1; depth < bounces; ++depth)
        {
            Hit hit;
            if (!trace(rayOrigin, rayDirection, hit))
                break;
            PathVertex p                = pathVertex(hit, rayOrigin, rayDirection);
            p.beta                      = beta;
            lightVertices[lightCount++] = p;
            if (depth + 1 == bounces)
                break;
            float3 l;
            float  pdf;
            if (!walkDirection(p, -rayDirection, state, l, pdf))
                break;
            // Importance transport: evaluate the radiance kernel in reverse.
            beta *= pathF(p, l, -rayDirection) * abs(dot(p.geometric, l)) / pdf;
            rayOrigin    = pathOrigin(p, l);
            rayDirection = l;
        }
    }
    for (uint e = 0; e < eyeCount; ++e)
    {
        if (any(eyeVertices[e].emission > 0))
            result += eyeVertices[e].beta * eyeVertices[e].emission * connectionWeight(e + 1, 0);
        if (e >= bounces)
            continue;
        float3 v = normalize(origin - eyeVertices[e].position);
        if (e > 0)
            v = normalize(eyeVertices[e - 1].position - eyeVertices[e].position);
        for (uint l = 0; l < lightCount && e + l + 1 <= bounces; ++l)
        {
            float3 delta     = lightVertices[l].position - eyeVertices[e].position;
            float  distance2 = dot(delta, delta);
            if (distance2 <= .000016)
                continue;
            float3 d      = normalize(delta);
            float3 fEye   = pathF(eyeVertices[e], v, d);
            float3 fLight = 1;
            if (l > 0)
                fLight =
                  pathF(lightVertices[l], -d, normalize(lightVertices[l - 1].position - lightVertices[l].position));
            if (!any(fEye > 0) || !any(fLight > 0))
                continue;
            float3 start = pathOrigin(eyeVertices[e], d), end = pathOrigin(lightVertices[l], -d), segment = end - start;
            Hit    shadow;
            if (traceRay(start, normalize(segment), length(segment) - .0006, shadow))
                continue;
            float geometry =
              abs(dot(eyeVertices[e].geometric, d)) * abs(dot(lightVertices[l].geometric, -d)) / distance2;
            float w = connectionWeight(e + 1, l + 1);
            result += eyeVertices[e].beta * lightVertices[l].beta * fEye * fLight * geometry * w;
        }
    }
    return min(max(result, 0), float3(64, 64, 64));
}

// Photon records and 32³ toroidal grid share the existing scratch binding.
// Adjacent 3³ cells always have distinct buckets; distant collisions are filtered
// by distance. Linked lists have one preallocated slot per emitted path/bounce.
uint photonCount()
{
    return asuint(parameters[29]).w;
}
uint photonHeads()
{
    uint4 f = asuint(parameters[24]);
    return f.y * f.z * 4;
}
uint photonRecords()
{
    return photonHeads() + 32768;
}
float photonRadius()
{
    float iteration = float(asuint(parameters[24]).x) / float(asuint(parameters[25]).x);
    return parameters[28].w * pow(1 + iteration, -1.0 / 6.0);
}
uint photonBucket(int3 cell)
{
    uint3 c = uint3(cell) & 31u;
    return c.x + c.y * 32 + c.z * 1024;
}
float3 scratchVector(uint at)
{
    return asfloat(uint3(work[at], work[at + 1], work[at + 2]));
}
void scratchVectorStore(uint at, float3 v)
{
    uint3 b      = asuint(v);
    work[at]     = b.x;
    work[at + 1] = b.y;
    work[at + 2] = b.z;
}
[numthreads(64, 1, 1)] void PhotonClear(uint3 id : SV_DispatchThreadID) {
    if (id.x < 32768)
        work[photonHeads() + id.x] = 0;
}[numthreads(64, 1, 1)] void PhotonMain(uint3 id : SV_DispatchThreadID)
{
    uint count = photonCount(), h = lightHeader(), bounces = asuint(parameters[24]).w;
    if (id.x >= count || texels[h] == 0)
        return;
    uint state = id.x * 1973u + asuint(parameters[24]).x * 9277u + 0x68bc21ebu;
    randomBits(state);
    uint   record    = texels[h + 1] + 4 * selectCDF(texels[h + 1], texels[h], 4, random(state));
    uint   primitive = texels[record + 2];
    float  r = sqrt(random(state)), s = random(state);
    float3 bary     = float3(1 - r, r * (1 - s), r * s);
    float3 position = vertices[primitive * 3].position.xyz * bary.x + vertices[primitive * 3 + 1].position.xyz * bary.y
                      + vertices[primitive * 3 + 2].position.xyz * bary.z;
    Hit source;
    source.primitive   = primitive;
    source.bary        = bary.yz;
    source.distance    = 0;
    PathVertex emitter = pathVertex(source, position, float3(0, 0, 1));
    float      pdf     = sourcePdf(emitter);
    if (pdf <= 0 || !any(emitter.emission > 0))
        return;
    float3 n         = random(state) < .5 ? emitter.geometric : -emitter.geometric;
    float3 direction = cosineDirection(n, state), origin = pathOrigin(emitter, direction),
           beta = emitter.emission * (2 * PI / pdf);
    for (uint depth = 1; depth <= bounces; ++depth)
    {
        Hit hit;
        if (!trace(origin, direction, hit))
            break;
        PathVertex p = pathVertex(hit, origin, direction);
        if (depth >= 2)
        {
            uint slot = id.x * bounces + depth - 1, at = photonRecords() + slot * 16;
            scratchVectorStore(at, p.position);
            scratchVectorStore(at + 3, -direction);
            scratchVectorStore(at + 6, beta);
            scratchVectorStore(at + 9, dot(p.geometric, -direction) < 0 ? -p.geometric : p.geometric);
            work[at + 13] = depth;
            uint previous;
            InterlockedExchange(
              work[photonHeads() + photonBucket(int3(floor(p.position / photonRadius())))], slot + 1, previous);
            work[at + 12] = previous;
        }
        if (depth == bounces)
            break;
        float3 l;
        float  pdfDirection;
        if (!walkDirection(p, -direction, state, l, pdfDirection))
            break;
        beta *= pathF(p, l, -direction) * abs(dot(p.geometric, l)) / pdfDirection;
        if (!any(beta > 0))
            break;
        origin    = pathOrigin(p, l);
        direction = l;
    }
}
float3 gatherPhotons(PathVertex p, float3 v, uint remaining)
{
    float  radius = photonRadius(), r2 = radius * radius;
    float3 n, g;
    pathFrame(p, v, n, g);
    int3   cell = int3(floor(p.position / radius));
    float3 flux = 0;
    for (int z = -1; z <= 1; ++z)
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
            {
                uint link = work[photonHeads() + photonBucket(cell + int3(x, y, z))];
                while (link != 0)
                {
                    uint   at    = photonRecords() + (link - 1) * 16;
                    float3 delta = scratchVector(at) - p.position;
                    float  d2    = dot(delta, delta);
                    if (
                      work[at + 13] <= remaining && d2 < r2 && abs(dot(delta, g)) < radius * .1
                      && dot(g, scratchVector(at + 9)) > .9)
                        flux += pathF(p, v, scratchVector(at + 3)) * scratchVector(at + 6) * (1 - d2 / r2);
                    link = work[at + 12];
                }
            }
    // Epanechnikov disk kernel integrates to one in surface-area measure.
    return flux * (2 / (PI * r2 * float(photonCount())));
}
float3 photonCamera(float3 origin, float3 direction, inout uint state, uint bounces)
{
    float3 result = 0, beta = 1;
    float  previousPdf = 0;
    for (uint depth = 0; depth <= bounces; ++depth)
    {
        Hit hit;
        if (!trace(origin, direction, hit))
            break;
        PathVertex p      = pathVertex(hit, origin, direction);
        float      pdfHit = emitterPdf(hit.primitive, origin, p.position) / max(areaProbability(), 1e-20);
        result += beta * p.emission * (depth == 0 ? 1 : misWeight(previousPdf, pdfHit));
        if (depth == bounces)
            break;
        float3 v      = -direction;
        bool   gather = p.metal < .5 && p.roughness > .2;
        // Pure finite-area NEE; no environment mixture, and no BSDF competitor
        // at a gather vertex because the camera walk stops there.
        uint   h = lightHeader(), record = texels[h + 1] + 4 * selectCDF(texels[h + 1], texels[h], 4, random(state));
        uint   primitive = texels[record + 2];
        float  r = sqrt(random(state)), s = random(state);
        float3 bary = float3(1 - r, r * (1 - s), r * s);
        Hit    lightHit;
        lightHit.primitive = primitive;
        lightHit.bary      = bary.yz;
        lightHit.distance  = 0;
        float3 lp = vertices[primitive * 3].position.xyz * bary.x + vertices[primitive * 3 + 1].position.xyz * bary.y
                    + vertices[primitive * 3 + 2].position.xyz * bary.z;
        PathVertex light = pathVertex(lightHit, lp, float3(0, 0, 1));
        float3     delta = lp - p.position, l = normalize(delta);
        float      cosine = abs(dot(light.geometric, -l));
        float      pdf    = cosine > 1e-8 ? sourcePdf(light) * dot(delta, delta) / cosine : 0;
        float3     f      = pathF(p, v, l);
        if (pdf > 0 && any(f > 0))
        {
            float3 start = pathOrigin(p, l), segment = pathOrigin(light, -l) - start;
            Hit    shadow;
            if (!traceRay(start, normalize(segment), length(segment) - .0006, shadow))
                result += beta * f * abs(dot(p.geometric, l)) * light.emission / pdf
                          * (gather ? 1 : misWeight(pdf, pathPdf(p, v, l)));
        }
        if (gather)
        {
            result += beta * gatherPhotons(p, v, bounces - depth);
            break;
        }
        float pdfDirection;
        if (!walkDirection(p, v, state, l, pdfDirection))
            break;
        beta *= pathF(p, v, l) * abs(dot(p.geometric, l)) / pdfDirection;
        previousPdf = pdfDirection;
        origin      = pathOrigin(p, l);
        direction   = l;
    }
    return min(max(result, 0), float3(64, 64, 64));
}

[numthreads(8, 8, 1)] void CSMain(uint3 id : SV_DispatchThreadID) {
    uint4 frame = asuint(parameters[24]), options = asuint(parameters[25]);
    if (id.x >= frame.y || id.y >= frame.z)
        return;
    uint index = id.y * frame.y + id.x;
    if ((options.w & 1u) != 0)
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
        uint4 bits          = asuint(guide);
        work[index * 4]     = bits.x;
        work[index * 4 + 1] = bits.y;
        work[index * 4 + 2] = bits.z;
        work[index * 4 + 3] = bits.w;
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
        bool   connectLightPaths = (options.w & 8u) != 0 && texels[lightHeader()] > 0;
        bool   usePhotons        = (options.w & 16u) != 0 && texels[lightHeader()] > 0;
        float3 value = path(parameters[4].xyz, direction, state, frame.w, !connectLightPaths && !usePhotons);
        if (connectLightPaths)
            value += bidirectional(parameters[4].xyz, direction, state, frame.w);
        if (usePhotons)
            value += photonCamera(parameters[4].xyz, direction, state, frame.w);
        sum += min(max(value, 0), float3(64, 64, 64));
    }
    accumulation[index] = float4(sum, 1);
    if ((options.w & 4u) != 0)
    {
        outputImage[index] = asuint(dot(sum / float(frame.x + options.x), float3(.2126, .7152, .0722)));
        return;
    }
    float3 color       = 1 - exp(-sum / float(frame.x + options.x) * parameters[4].w);
    uint3  encoded     = uint3(saturate(pow(max(color, 0), 1 / 2.2)) * 255 + .5);
    outputImage[index] = encoded.x | (encoded.y << 8) | (encoded.z << 16) | 0xff000000u;
}

  // Display-only joint bilateral filter. Raw progressive radiance is never modified.
  // Geometry guides stop filtering across silhouettes, depth breaks and normal edges.
  [numthreads(8, 8, 1)] void DenoiseMain(uint3 id : SV_DispatchThreadID)
{
    uint4 frame = asuint(parameters[24]), options = asuint(parameters[25]);
    if (id.x >= frame.y || id.y >= frame.z)
        return;
    uint index = id.y * frame.y + id.x;
    if ((options.w & 4u) != 0)
        return;
    float4 centerGuide = asfloat(uint4(work[index * 4], work[index * 4 + 1], work[index * 4 + 2], work[index * 4 + 3]));
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
            float4 guide    = asfloat(
              uint4(work[neighbor * 4], work[neighbor * 4 + 1], work[neighbor * 4 + 2], work[neighbor * 4 + 3]));
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
