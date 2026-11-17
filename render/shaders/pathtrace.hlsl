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
// Eager primary sample vectors make unused dimensions and rejected proposals
// well-defined across all path-length changes (maximum 8 bounces).
static uint       mltMode = 0, mltDimension = 0, mltSeed = 0, mltAddress = 0;
static bool       mltLarge       = false;
static const uint MLT_DIMENSIONS = 256, MLT_MAX_CHAINS = 1024;
uint              mltHash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}
float mltUniform(uint seed, uint dimension)
{
    return (mltHash(seed + dimension * 0x9e3779b9u) >> 8) * 0.000000059604644775390625;
}
uint mltPixels()
{
    uint4 f = asuint(parameters[24]);
    return f.y * f.z;
}
uint mltHeader()
{
    return mltPixels() * 14;
}
uint mltBootstrapCount()
{
    return asuint(parameters[26]).w;
}
uint mltBootstrapOffset()
{
    return mltHeader() + 16;
}
uint mltChainOffset()
{
    return mltBootstrapOffset() + 2 * mltBootstrapCount();
}
uint mltChainCount()
{
    return min(MLT_MAX_CHAINS, mltPixels());
}
float mltPrimary(uint dimension)
{
    if (mltMode == 1 || mltLarge)
        return mltUniform(mltSeed, dimension);
    float current = asfloat(work[mltAddress + dimension]);
    if (mltMode == 3)
        return current;
    uint  bits      = mltHash(mltSeed + dimension * 0x9e3779b9u);
    float magnitude = (float(bits >> 9) + .5) * 0.00000011920928955078125;
    float delta     = magnitude * asfloat(asuint(parameters[31]).w) * ((bits & 1u) != 0 ? 1 : -1);
    return frac(current + delta);
}
float random(inout uint state)
{
    if (mltMode != 0)
    {
        uint dimension = mltDimension++;
        if (dimension >= MLT_DIMENSIONS)
        {
            uint ignored;
            InterlockedAdd(work[mltHeader() + 3], 1, ignored);
            return .5;
        }
        return mltPrimary(dimension);
    }
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
struct SurfaceMaterial
{
    float4 base;
    float4 factors;
    float4 emission;
    float4 uv;
};
SurfaceMaterial surfaceMaterial(Vertex vertex)
{
    SurfaceMaterial m;
    m.base     = parameters[5];
    m.factors  = parameters[6];
    m.emission = parameters[7];
    m.uv       = float4(1, 1, 0, 0);
    if (asuint(parameters[27]).x > 0)
    {
        uint at    = (uint(max(1, vertex.position.w)) - 1) * 16;
        m.base     = asfloat(uint4(texels[at], texels[at + 1], texels[at + 2], texels[at + 3]));
        m.factors  = asfloat(uint4(texels[at + 4], texels[at + 5], texels[at + 6], texels[at + 7]));
        m.emission = asfloat(uint4(texels[at + 8], texels[at + 9], texels[at + 10], texels[at + 11]));
        m.uv       = asfloat(uint4(texels[at + 12], texels[at + 13], texels[at + 14], texels[at + 15]));
    }
    return m;
}
float2 materialUV(float2 uv, SurfaceMaterial m)
{
    return frac(uv) * m.uv.xy + m.uv.zw;
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
bool traceRay(float3 origin, float3 direction, float maxDistance, out Hit hit, bool shadowOnly = false)
{
    // Hardware queries require TMax >= TMin, including sources inside the bias region.
    if (maxDistance <= .0005) { hit.distance=0;hit.primitive=0;hit.bary=0;return false; }
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
            if (shadowOnly && vertices[primitive * 3].uv.z < 0)
                continue;
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
    query.TraceRayInline(scene, shadowOnly ? RAY_FLAG_FORCE_NON_OPAQUE : RAY_FLAG_NONE, 255, ray);
    while (query.Proceed())
    {
        if (
          shadowOnly && query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE
          && vertices[query.CandidatePrimitiveIndex() * 3].uv.z >= 0)
            query.CommitNonOpaqueTriangleHit();
    }
    bool found    = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    hit.distance  = found ? query.CommittedRayT() : 0;
    hit.primitive = found ? query.CommittedPrimitiveIndex() : 0;
    hit.bary      = found ? query.CommittedTriangleBarycentrics() : float2(0, 0);
    return found;
#endif
}
// Camera projection uses existing scene padding; secondary transport is not camera-clipped.
void cameraRay(float2 p, out float3 origin, out float3 direction)
{
    float3 offset = p.x * parameters[10].w * parameters[10].xyz - p.y * abs(parameters[11].w) * parameters[11].xyz;
    bool   orthographic = parameters[11].w < 0;
    origin              = parameters[4].xyz + (orthographic ? offset : float3(0, 0, 0));
    direction           = normalize(parameters[12].xyz + (orthographic ? float3(0, 0, 0) : offset));
}
bool traceCamera(float3 origin, float3 direction, out Hit hit)
{
    float nearPlane = parameters[16].w, farPlane = parameters[17].w;
    if (farPlane <= nearPlane)
        return traceRay(origin, direction, 1000, hit);
    float cosine = max(dot(direction, parameters[12].xyz), .000001);
    float start  = nearPlane / cosine;
    bool  found  = traceRay(origin + direction * start, direction, (farPlane - nearPlane) / cosine, hit);
    hit.distance += start;
    return found;
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
uint selectLightCDF(uint offset, uint count, uint stride, float u)
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
        uint   record    = texels[h + 1] + 4 * selectLightCDF(texels[h + 1], texels[h], 4, random(state));
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
        radiance    = emitters ? pow(max(textureColor(4, materialUV(uv, surfaceMaterial(a))).rgb, 0), 2.2)
                                * surfaceMaterial(a).emission.rgb
                               : float3(0, 0, 0);
    }
    else
    {
        uint grid = texels[h + 4];
        if (grid == 0 || parameters[13].z <= 0)
            return false;
        uint   i    = selectLightCDF(texels[h + 3], 6 * grid * grid, 1, random(state));
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
    float  transmission;
    float  ior;
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
    SurfaceMaterial mat       = surfaceMaterial(a);
    float2          textureUV = materialUV(uv, mat);
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
        float3 mapped = textureColor(1, textureUV).xyz * 2 - 1;
        mapped.xy *= mat.factors.z;
        p.normal = normalize(t * mapped.x + bit * mapped.y + p.normal * mapped.z);
    }
    p.base         = pow(max(textureColor(0, textureUV).rgb, 0), 2.2) * mat.base.rgb;
    p.emission     = pow(max(textureColor(4, textureUV).rgb, 0), 2.2) * mat.emission.rgb;
    float4 mr      = textureColor(2, textureUV);
    p.metal        = saturate(mr.b * mat.factors.x);
    p.roughness    = clamp(mr.g * mat.factors.y, .06, 1);
    p.transmission = a.normal.w;
    p.ior          = max(a.uv.w, 1.01);
    p.beta         = 1;
    p.primitive    = hit.primitive;
    return p;
}
void pathFrame(PathVertex p, float3 v, out float3 n, out float3 g)
{
    n = dot(p.normal, v) < 0 ? -p.normal : p.normal;
    g = dot(p.geometric, v) < 0 ? -p.geometric : p.geometric;
    if (dot(n, v) <= 1e-6)
        n = g;
}
// Rough dielectric, NDF sampling and solid-angle PDFs. Radiance transport;
// reversing v/l supplies the importance kernel for light-subpath walking.
float dielectricFresnel(float cosine, float eta)
{
    float sin2 = max(0, 1 - cosine * cosine) / (eta * eta);
    if (sin2 >= 1)
        return 1;
    float ct = sqrt(1 - sin2);
    float a  = (cosine - eta * ct) / (cosine + eta * ct);
    float b  = (eta * cosine - ct) / (eta * cosine + ct);
    return .5 * (a * a + b * b);
}
float glassD(float nh, float roughness)
{
    float a2 = pow(roughness, 4), q = nh * nh * (a2 - 1) + 1;
    return a2 / max(PI * q * q, 1e-20);
}
float glassG1(float cosine, float roughness)
{
    float c2 = cosine * cosine;
    return 2 / (1 + sqrt(1 + pow(roughness, 4) * max(0, 1 - c2) / max(c2, 1e-20)));
}
void glassKernel(PathVertex p, float3 v, float3 l, out float value, out float pdf)
{
    value      = 0;
    pdf        = 0;
    float3 n   = dot(p.geometric, v) > 0 ? p.geometric : -p.geometric;
    float  eta = dot(p.geometric, v) > 0 ? p.ior : 1 / p.ior;
    float  nv = dot(n, v), nl = dot(n, l);
    bool   reflection = nl > 0;
    float3 h          = v + l * (reflection ? 1 : eta);
    if (dot(h, h) < 1e-20 || abs(nl) < 1e-7 || nv < 1e-7)
        return;
    h = normalize(h);
    if (dot(h, n) < 0)
        h = -h;
    float vh = dot(v, h), lh = dot(l, h);
    if (vh <= 0 || lh * nl <= 0)
        return;
    float f = dielectricFresnel(vh, eta), d = glassD(dot(n, h), p.roughness);
    float g = glassG1(nv, p.roughness) * glassG1(abs(nl), p.roughness);
    if (reflection)
    {
        value = f * d * g / (4 * nv * nl);
        pdf   = f * d * dot(n, h) / (4 * abs(vh));
    }
    else
    {
        float denom = lh + vh / eta;
        denom *= denom;
        pdf   = (1 - f) * d * dot(n, h) * abs(lh) / max(denom, 1e-20);
        value = (1 - f) * d * g * abs(lh * vh / (nl * nv * max(denom, 1e-20))) / (eta * eta);
    }
}
// Radiance BSDF expressed against geometric projected area. Reversing v/l for
// light walking supplies the adjoint shading-normal correction, without assuming
// a shading normal equals the geometric normal.
float3 pathF(PathVertex p, float3 v, float3 l)
{
    float glassValue, glassPdf;
    glassValue = 0;
    glassPdf   = 0;
    if (p.transmission > 0)
        glassKernel(p, v, l, glassValue, glassPdf);
    float3 transmitted = p.transmission * glassValue;
    float3 n, g;
    pathFrame(p, v, n, g);
    float cosine = dot(g, l);
    if (cosine <= 1e-7 || dot(n, l) <= 0)
        return transmitted;
    return transmitted + (1 - p.transmission) * brdf(n, v, l, p.base, p.metal, p.roughness) * dot(n, l) / cosine;
}
float pathPdf(PathVertex p, float3 v, float3 l)
{
    float3 n, g;
    pathFrame(p, v, n, g);
    float value = 0, pdf = 0;
    if (p.transmission > 0)
        glassKernel(p, v, l, value, pdf);
    return p.transmission * pdf + (1 - p.transmission) * (dot(g, l) > 0 ? bsdfPdf(n, v, l, p.metal, p.roughness) : 0);
}
float3 cosineDirection(float3 n, inout uint state)
{
    float3 t = normalize(cross(abs(n.y) < .999 ? float3(0, 1, 0) : float3(1, 0, 0), n)), b = cross(n, t);
    float  q = random(state), phi = 2 * PI * random(state);
    return t * (cos(phi) * sqrt(q)) + b * (sin(phi) * sqrt(q)) + n * sqrt(1 - q);
}
bool walkDirection(PathVertex p, float3 v, inout uint state, out float3 l, out float pdf)
{
    if (p.transmission > 0 && random(state) < p.transmission)
    {
        float3 n   = dot(p.geometric, v) > 0 ? p.geometric : -p.geometric;
        float  eta = dot(p.geometric, v) > 0 ? p.ior : 1 / p.ior;
        float3 t = normalize(cross(abs(n.y) < .999 ? float3(0, 1, 0) : float3(1, 0, 0), n)), b = cross(n, t);
        float  q = random(state), phi = 2 * PI * random(state), a2 = pow(p.roughness, 4);
        float  c  = sqrt((1 - q) / (1 + (a2 - 1) * q));
        float3 h  = t * cos(phi) * sqrt(max(0, 1 - c * c)) + b * sin(phi) * sqrt(max(0, 1 - c * c)) + n * c;
        float  vh = dot(v, h);
        if (vh <= 0)
        {
            l   = 0;
            pdf = 0;
            return false;
        }
        bool reflection = random(state) < dielectricFresnel(vh, eta);
        l               = reflection ? reflect(-v, h) : refract(-v, h, 1 / eta);
        if (dot(l, l) < .5 || (dot(n, l) > 0) != reflection)
        {
            pdf = 0;
            return false;
        }
        pdf = pathPdf(p, v, l);
        return pdf > 0;
    }
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
// Local analytic sources have delta position, with directional density in solid angle.
// The area-emitter table remains one source group; each active local light is
// another group. Uniform group selection has full support, including dark scenes.
uint localSourceCount()
{
    uint4 info  = asuint(parameters[32]);
    uint  count = 0;
    if (info.x != 0)
        for (uint i = 0; i < info.y; ++i)
            if (parameters[35 + 4 * i].w > .5 && parameters[33 + 4 * i].w > 0 && any(parameters[33 + 4 * i].rgb > 0))
                ++count;
    return count;
}
uint sourceGroupCount()
{
    return localSourceCount() + (texels[lightHeader()] > 0 ? 1u : 0u);
}
uint localSourceIndex(uint selected)
{
    uint count = 0;
    for (uint i = 0; i < asuint(parameters[32]).y; ++i)
        if (parameters[35 + 4 * i].w > .5 && parameters[33 + 4 * i].w > 0 && any(parameters[33 + 4 * i].rgb > 0))
        {
            if (count++ == selected)
                return i;
        }
    return 0;
}
bool deltaSource(PathVertex p)
{
    return p.primitive >= 0xfffffff0u;
}
uint deltaIndex(PathVertex p)
{
    return 0xffffffffu - p.primitive;
}
float sourceFalloff(uint index, float3 direction)
{
    uint   at   = 33 + 4 * index;
    float4 axis = parameters[at + 2], options = parameters[at + 3];
    if (axis.w < 1.5)
        return 1;
    float c = dot(axis.xyz, direction);
    return options.x > options.y + 1e-6 ? smoothstep(options.y, options.x, c) : step(options.y, c);
}
float sourceDistanceFalloff(uint index, float distanceSquared)
{
    float range = parameters[34 + 4 * index].w;
    // Match the direct estimator's inverse-square near-source clamp and range.
    return min(distanceSquared / .01, 1)
           * (range > 0 ? pow(saturate(1 - pow(sqrt(distanceSquared) / range, 4)), 2) : 1);
}
float sourceDirectionPdf(PathVertex p, float3 direction)
{
    if (!deltaSource(p))
        return abs(dot(p.geometric, direction)) / (2 * PI);
    uint   at     = 33 + 4 * deltaIndex(p);
    float4 axis   = parameters[at + 2];
    float  cosine = parameters[at + 3].y;
    return axis.w < 1.5 ? 1 / (4 * PI)
                        : (dot(axis.xyz, direction) >= cosine ? 1 / max(2 * PI * (1 - cosine), 1e-20) : 0);
}
// Analytic direct lighting for PT and the photon camera pass.
// They are delta lights (not visible emitter geometry); visibility softness is a filter.
float3 analyticLights(
  PathVertex surface, float3 v, inout uint state, bool includeLocal = true, bool includeDirectional = true)
{
    uint4  info = asuint(parameters[32]);
    float3 n, geometric;
    pathFrame(surface, v, n, geometric);
    float3 sum = 0;
    // Share one disk sample across lights: correlated but unbiased visibility,
    // bounded to two additional primary dimensions per scattering vertex.
    float radiusSample = 0, angleSample = 0;
    if (info.z != 0 && info.y > 0)
    {
        radiusSample = sqrt(random(state));
        angleSample  = random(state) * 2 * PI;
    }
    [loop] for (uint i = 0; i < info.y; ++i)
    {
        uint   at    = 33 + 4 * i;
        float4 color = parameters[at], positionRange = parameters[at + 1], directionType = parameters[at + 2],
               options = parameters[at + 3];
        if (color.w <= 0)
            continue;
        uint   type        = uint(directionType.w);
        if ((type != 0 && !includeLocal) || (type == 0 && !includeDirectional))
            continue;
        float3 l           = directionType.xyz;
        float  attenuation = 1, maxDistance = 1e30;
        if (type != 0)
        {
            float3 delta           = positionRange.xyz - surface.position;
            float  distanceSquared = dot(delta, delta), distance = sqrt(distanceSquared);
            l           = distance > 1e-6 ? delta / distance : float3(0, 1, 0);
            attenuation = 1 / max(distanceSquared, .01);
            if (positionRange.w > 0)
                attenuation *= pow(saturate(1 - pow(distance / positionRange.w, 4)), 2);
            if (type == 2)
            {
                float cone = dot(-l, directionType.xyz);
                attenuation *=
                  options.x > options.y + 1e-6 ? smoothstep(options.y, options.x, cone) : step(options.y, cone);
            }
            maxDistance = distance;
        }
        // A dielectric can scatter into either geometric hemisphere. Test
        // the full BSDF instead of applying an opaque reflection-only mask.
        float3 f = pathF(surface, v, l);
        if (attenuation <= 0 || !any(f > 0))
            continue;
        bool visible = true;
        if (info.z != 0 && options.z > .5)
        {
            // Offset toward the sampled outgoing direction, including transmission.
            float  side   = dot(geometric, l) < 0 ? -1 : 1;
            float3 origin = surface.position + geometric * (side * asfloat(info.w)), ray = l;
            float3 axis = abs(l.y) < .99 ? float3(0, 1, 0) : float3(1, 0, 0), t = normalize(cross(l, axis)),
                   b      = cross(l, t);
            float3 offset = 0;
            if (options.w > 0)
            {
                float r = radiusSample * options.w * .006;
                offset  = r * (cos(angleSample) * t + sin(angleSample) * b);
            }
            if (type == 0)
                ray = normalize(l + offset);
            else
            {
                float3 delta = positionRange.xyz + offset - origin;
                maxDistance  = length(delta);
                ray          = maxDistance > 1e-6 ? delta / maxDistance : l;
            }
            Hit blocker;
            visible = maxDistance <= 1e-5 || !traceRay(origin, ray, max(0, maxDistance - 1e-5), blocker, true);
        }
        if (visible)
            sum += f * abs(dot(surface.geometric, l)) * color.rgb * color.w * attenuation;
    }
    return sum;
}
float3 path(
  float3     origin,
  float3     direction,
  inout uint state,
  uint       bounces,
  bool       emitters     = true,
  uint       minScatters  = 0,
  bool       includeLocal = true)
{
    float3 sum = 0, weight = 1;
    float  previousPdf = 0;
    bool   useMIS      = !emitters || (asuint(parameters[25]).w & 2u) != 0;
    for (uint depth = 0; depth <= bounces; ++depth)
    {
        Hit hit;
        bool found;
        [branch] if (depth == 0) found = traceCamera(origin, direction, hit);
        else found                     = trace(origin, direction, hit);
        if (!found)
        {
            if (depth < minScatters)
                break;
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
          (emitters && depth >= minScatters ? 1 : 0) * weight * surface.emission
          * (useMIS && depth > 0 ? misWeight(previousPdf, emitterPdf(hit.primitive, origin, surface.position)) : 1);
        if (
          depth == bounces
          || (!emitters && parameters[13].z <= 0 && parameters[9].w <= 0 && asuint(parameters[32]).x == 0))
            break;
        float3 v = -direction, n, geometric;
        pathFrame(surface, v, n, geometric);
        float  metal = surface.metal, roughness = surface.roughness;
        float3 position = origin + direction * hit.distance, light = parameters[8].xyz;
        if (depth + 1 >= minScatters && asuint(parameters[32]).x != 0)
            sum += weight * analyticLights(surface, v, state, includeLocal);
        if (
          asuint(parameters[32]).x == 0 && depth + 1 >= minScatters && parameters[9].w > 0
          && any(pathF(surface, v, light) > 0))
        {
            Hit shadow;
            if (!traceRay(pathOrigin(surface, light), light, 10000, shadow, true))
                sum += weight * pathF(surface, v, light) * abs(dot(surface.geometric, light)) * parameters[9].rgb
                       * parameters[9].w;
        }
        if (useMIS && depth + 1 >= minScatters)
        {
            float3 l, radiance;
            float  pdf, maxDistance;
            float3 nextOrigin = position + geometric * .001;
            if (sampleLight(position, state, l, radiance, pdf, maxDistance, emitters))
            {
                Hit shadow;
                if (!traceRay(pathOrigin(surface, l), l, maxDistance, shadow, true))
                    sum += weight * pathF(surface, v, l) * abs(dot(surface.geometric, l)) * radiance / pdf
                           * misWeight(pdf, pathPdf(surface, v, l));
            }
        }
        float3 l;
        float  pdf;
        if (!walkDirection(surface, v, state, l, pdf))
            break;
        previousPdf = pdf;
        weight *= pathF(surface, v, l) * abs(dot(surface.geometric, l)) / pdf;
        if (depth >= 2)
        {
            float survive = clamp(max(weight.x, max(weight.y, weight.z)), .1, .95);
            if (random(state) > survive)
                break;
            weight /= survive;
        }
        direction = l;
        origin    = pathOrigin(surface, l);
    }
    return max(sum, 0);
}
float sourcePdf(PathVertex p)
{
    if (deltaSource(p))
        return 1; // Discrete position mass; group selection is applied separately.
    float3 a = vertices[p.primitive * 3].position.xyz, b = vertices[p.primitive * 3 + 1].position.xyz,
           c   = vertices[p.primitive * 3 + 2].position.xyz;
    float area = .5 * length(cross(b - a, c - a));
    return area > 0 ? asfloat(texels[texels[lightHeader() + 2] + p.primitive]) / area : 0;
}
// Sample position and initial ray once for BDPT and photon tracing. Area PDFs
// are continuous; the point/spot source position uses a discrete mass instead.
bool sampleSource(inout uint state, out PathVertex emitter, out float3 origin, out float3 direction, out float3 beta)
{
    emitter = (PathVertex)0;
    origin = direction = beta = 0;
    uint groups               = sourceGroupCount();
    if (groups == 0)
        return false;
    bool area     = texels[lightHeader()] > 0;
    uint selected = min(uint(random(state) * groups), groups - 1);
    if (area && selected == 0)
    {
        uint   h         = lightHeader();
        uint   record    = texels[h + 1] + 4 * selectLightCDF(texels[h + 1], texels[h], 4, random(state));
        uint   primitive = texels[record + 2];
        float  r = sqrt(random(state)), s = random(state);
        float3 bary = float3(1 - r, r * (1 - s), r * s);
        Hit    source;
        source.primitive = primitive;
        source.bary      = bary.yz;
        source.distance  = 0;
        float3 position  = vertices[primitive * 3].position.xyz * bary.x
                          + vertices[primitive * 3 + 1].position.xyz * bary.y
                          + vertices[primitive * 3 + 2].position.xyz * bary.z;
        emitter   = pathVertex(source, position, float3(0, 0, 1));
        float pdf = sourcePdf(emitter) / groups;
        if (pdf <= 0)
            return false;
        emitter.beta = emitter.emission / pdf;
        direction    = cosineDirection(random(state) < .5 ? emitter.geometric : -emitter.geometric, state);
        origin       = pathOrigin(emitter, direction);
        beta         = emitter.beta * (2 * PI);
    }
    else
    {
        uint index = localSourceIndex(selected - (area ? 1u : 0u)), at = 33 + 4 * index;
        emitter.position  = parameters[at + 1].xyz;
        emitter.primitive = 0xffffffffu - index;
        emitter.emission  = parameters[at].rgb * parameters[at].w;
        emitter.beta      = emitter.emission * groups;
        float4 axis       = parameters[at + 2];
        float  c          = lerp(1, axis.w < 1.5 ? -1 : parameters[at + 3].y, random(state));
        float  phi = 2 * PI * random(state), r = sqrt(max(0, 1 - c * c));
        float3 n  = axis.w < 1.5 ? float3(0, 1, 0) : axis.xyz;
        float3 t  = normalize(cross(abs(n.y) < .999 ? float3(0, 1, 0) : float3(1, 0, 0), n));
        direction = t * (r * cos(phi)) + cross(n, t) * (r * sin(phi)) + n * c;
        origin    = emitter.position;
        beta      = emitter.beta * sourceFalloff(index, direction) / sourceDirectionPdf(emitter, direction);
    }
    return any(beta > 0);
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
    bool       deltaEndpoint = deltaSource(endpoint);
    float      pdfSource     = sourcePdf(endpoint) / max(1u, sourceGroupCount());
    if (pdfSource <= 0)
        return lightCount == 0 ? 1 : 0;
    // Adjacent strategy density ratios: p_k/p_(k+1). The common camera-ray
    // density and all unchanged path edges cancel. Stream log-sum-exp so no
    // duplicate full-path array or per-strategy scratch arrays are needed.
    // A camera walk cannot hit a delta-position emitter. Begin at the last
    // valid strategy (one source vertex), excluding eye-only from MIS entirely.
    float current = 0, maximum = 0, total = 1;
    uint  first  = deltaEndpoint ? count - 1 : count;
    float chosen = selected == first ? 0 : -1e30;
    for (int k = int(first) - 1; k >= 1; --k)
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
                pdf = sourceDirectionPdf(fromLight, d);
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
float3 bidirectional(float3 origin, float3 direction, inout uint state, uint bounces, uint minScatters = 0)
{
    // Finite-area and delta-position sources share the same surface strategies.
    // Environment and directional sources retain the camera-path estimator.
    uint   header   = lightHeader();
    float3 result   = 0;
    uint   eyeCount = 0, lightCount = 0;
    float3 beta = 1, rayOrigin = origin, rayDirection = direction;
    // No roulette: every strategy of a bounded path length has one sample.
    for (uint depth = 0; depth <= bounces; ++depth)
    {
        Hit hit;
        bool found;
        [branch] if (depth == 0) found = traceCamera(rayOrigin, rayDirection, hit);
        else found                     = trace(rayOrigin, rayDirection, hit);
        if (!found)
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
    PathVertex emitter;
    if (sampleSource(state, emitter, rayOrigin, rayDirection, beta))
    {
        lightVertices[lightCount++] = emitter;
        for (uint depth = 1; depth < bounces; ++depth)
        {
            Hit hit;
            if (!trace(rayOrigin, rayDirection, hit))
                break;
            PathVertex p                = pathVertex(hit, rayOrigin, rayDirection);
            if (depth == 1 && deltaSource(emitter))
            {
                float3 delta = p.position - emitter.position;
                beta *= sourceDistanceFalloff(deltaIndex(emitter), dot(delta, delta));
            }
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
    float diskRadius = 0, diskAngle = 0;
    if (localSourceCount() > 0 && asuint(parameters[32]).z != 0)
    {
        diskRadius = sqrt(random(state));
        diskAngle  = 2 * PI * random(state);
    }
    for (uint e = 0; e < eyeCount; ++e)
    {
        if (texels[header] > 0 && e >= minScatters && any(eyeVertices[e].emission > 0))
            result += eyeVertices[e].beta * eyeVertices[e].emission * connectionWeight(e + 1, 0);
        if (e >= bounces)
            continue;
        float3 v = normalize(origin - eyeVertices[e].position);
        if (e > 0)
            v = normalize(eyeVertices[e - 1].position - eyeVertices[e].position);
        for (uint l = 0; l < lightCount && e + l + 1 <= bounces; ++l)
        {
            if (e + l + 1 < minScatters)
                continue;
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
            bool   deltaConnection = l == 0 && deltaSource(lightVertices[l]);
            bool   shadowEnabled   = true;
            float  sourceFactor    = abs(dot(lightVertices[l].geometric, -d));
            [branch] if (deltaConnection)
            {
                uint   at = 33 + 4 * deltaIndex(lightVertices[l]);
                float3 t  = normalize(cross(abs(d.y) < .99 ? float3(0, 1, 0) : float3(1, 0, 0), d));
                float3 offset =
                  (t * cos(diskAngle) + cross(d, t) * sin(diskAngle)) * diskRadius * parameters[at + 3].w * .006;
                float3 n, g;
                pathFrame(eyeVertices[e], v, n, g);
                // Delta-source connections can transmit through the current interface.
                // Offsetting toward the eye would immediately hit that same surface.
                float side    = dot(g, d) < 0 ? -1 : 1;
                start         = eyeVertices[e].position + g * (side * asfloat(asuint(parameters[32]).w));
                end           = lightVertices[l].position + offset;
                shadowEnabled = asuint(parameters[32]).z != 0 && parameters[at + 3].z > .5;
                sourceFactor  = sourceFalloff(deltaIndex(lightVertices[l]), -d)
                               * sourceDistanceFalloff(deltaIndex(lightVertices[l]), distance2);
            }
            segment = end - start;
            if (
              shadowEnabled && length(segment) > .001
              && traceRay(start, normalize(segment), length(segment) - .0006, shadow, true))
                continue;
            float geometry = abs(dot(eyeVertices[e].geometric, d)) * sourceFactor / distance2;
            float w = connectionWeight(e + 1, l + 1);
            result += eyeVertices[e].beta * lightVertices[l].beta * fEye * fLight * geometry * w;
        }
    }
    return max(result, 0);
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
    if (id.x >= count || sourceGroupCount() == 0)
        return;
    uint state = id.x * 1973u + asuint(parameters[24]).x * 9277u + 0x68bc21ebu;
    randomBits(state);
    PathVertex emitter;
    float3     direction, origin, beta;
    if (!sampleSource(state, emitter, origin, direction, beta))
        return;
    for (uint depth = 1; depth <= bounces; ++depth)
    {
        Hit hit;
        if (!trace(origin, direction, hit))
            break;
        PathVertex p = pathVertex(hit, origin, direction);
        if (depth == 1 && deltaSource(emitter))
        {
            float3 delta = p.position - emitter.position;
            beta *= sourceDistanceFalloff(deltaIndex(emitter), dot(delta, delta));
        }
        if (depth >= 2 && p.transmission == 0)
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
        bool found;
        [branch] if (depth == 0) found = traceCamera(origin, direction, hit);
        else found                     = trace(origin, direction, hit);
        if (!found)
            break;
        PathVertex p      = pathVertex(hit, origin, direction);
        float      pdfHit = emitterPdf(hit.primitive, origin, p.position) / max(areaProbability(), 1e-20);
        result += beta * p.emission * (depth == 0 ? 1 : misWeight(previousPdf, pdfHit));
        if (depth == bounces)
            break;
        float3 v      = -direction;
        bool   gather = p.transmission == 0 && p.metal < .5 && p.roughness > .2;
        if (asuint(parameters[32]).x != 0)
            result += beta * analyticLights(p, v, state, true, false);
        float3 l;
        // Pure finite-area NEE; no environment mixture, and no BSDF competitor
        // at a gather vertex because the camera walk stops there.
        if (texels[lightHeader()] > 0)
        {
            uint h           = lightHeader(),
                 record      = texels[h + 1] + 4 * selectLightCDF(texels[h + 1], texels[h], 4, random(state));
            uint   primitive = texels[record + 2];
            float  r = sqrt(random(state)), s = random(state);
            float3 bary = float3(1 - r, r * (1 - s), r * s);
            Hit    lightHit;
            lightHit.primitive = primitive;
            lightHit.bary      = bary.yz;
            lightHit.distance  = 0;
            float3 lp          = vertices[primitive * 3].position.xyz * bary.x
                        + vertices[primitive * 3 + 1].position.xyz * bary.y
                        + vertices[primitive * 3 + 2].position.xyz * bary.z;
            PathVertex light = pathVertex(lightHit, lp, float3(0, 0, 1));
            float3     delta = lp - p.position;
            l                = normalize(delta);
            float  cosine    = abs(dot(light.geometric, -l));
            float  pdf       = cosine > 1e-8 ? sourcePdf(light) * dot(delta, delta) / cosine : 0;
            float3 f         = pathF(p, v, l);
            if (pdf > 0 && any(f > 0))
            {
                float3 start = pathOrigin(p, l), segment = pathOrigin(light, -l) - start;
                Hit    shadow;
                if (!traceRay(start, normalize(segment), length(segment) - .0006, shadow, true))
                    result += beta * f * abs(dot(p.geometric, l)) * light.emission / pdf
                              * (gather ? 1 : misWeight(pdf, pathPdf(p, v, l)));
            }
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
    return max(result, 0);
}

// Global PSSMLT film. The indirect target is BDPT luminance; the independent
// one-scatter estimator is added in CSMain. All work uses the existing binding.
float3 mltEvaluate(out uint pixel)
{
    mltDimension     = 0;
    uint   state     = 0;
    float2 uv        = float2(random(state), random(state));
    uint2  size      = asuint(parameters[24]).yz;
    uint2  location  = min(uint2(uv * float2(size)), size - 1);
    pixel            = location.y * size.x + location.x;
    float2 p         = uv * 2 - 1;
    float3 origin, direction;
    cameraRay(p, origin, direction);
    bool   connect = sourceGroupCount() > 0;
    float3 value   = path(origin, direction, state, asuint(parameters[24]).w, !connect, 2, !connect);
    if (connect)
        value += bidirectional(origin, direction, state, asuint(parameters[24]).w, 2);
    return max(value, 0);
}
float mltIntensity(float3 value)
{
    return dot(value, float3(.2126, .7152, .0722));
}
// 64-bit fixed-point RGB splats using only core 32-bit atomics. The low
// word has 16 fractional bits; carry is recorded separately after atomic add.
void mltAtomicAdd(uint offset, float value)
{
    uint increment = uint(max(value, 0) * 65536 + .5), previous;
    InterlockedAdd(work[offset], increment, previous);
    if (previous > 0xffffffffu - increment)
    {
        uint ignored;
        InterlockedAdd(work[offset + 1], 1, ignored);
    }
}
float mltFilmChannel(uint offset)
{
    return (float(work[offset + 1]) * 4294967296.0 + float(work[offset])) / 65536;
}
void mltSplat(uint pixel, float3 value, float intensity, float weight)
{
    if (intensity <= 0 || weight <= 0)
        return;
    float3 contribution = value * (weight / intensity);
    uint   address      = mltPixels() * 4 + pixel * 6;
    mltAtomicAdd(address, contribution.x);
    mltAtomicAdd(address + 2, contribution.y);
    mltAtomicAdd(address + 4, contribution.z);
}
[numthreads(64, 1, 1)] void MLTBootstrap(uint3 id : SV_DispatchThreadID) {
    uint i = id.x + id.y * 65536u;
    if (i < mltPixels())
    {
        for (uint k = 0; k < 6; k++)
            work[mltPixels() * 4 + i * 6 + k] = 0;
        for (uint k = 0; k < 4; k++)
            work[mltPixels() * 10 + i * 4 + k] = 0;
    }
    if (i >= mltBootstrapCount())
        return;
    mltMode  = 1;
    mltLarge = true;
    mltSeed  = mltHash(i + asuint(parameters[14]).z * 0x68bc21ebu + 12345u);
    uint   pixel;
    float3 value                           = mltEvaluate(pixel);
    work[mltBootstrapOffset() + i * 2]     = mltSeed;
    work[mltBootstrapOffset() + i * 2 + 1] = asuint(mltIntensity(value));
}[numthreads(1, 1, 1)] void MLTReduce(uint3 id : SV_DispatchThreadID)
{
    precise float sum = 0, compensation = 0;
    float         square   = 0;
    uint          positive = 0;
    for (uint i = 0; i < mltBootstrapCount(); i++)
    {
        uint          at        = mltBootstrapOffset() + i * 2 + 1;
        float         weight    = asfloat(work[at]);
        precise float corrected = weight - compensation, next = sum + corrected;
        compensation = (next - sum) - corrected;
        sum          = next;
        square += weight * weight;
        positive += weight > 0 ? 1 : 0;
        work[at] = asuint(sum);
    }
    float mean            = sum / float(mltBootstrapCount());
    work[mltHeader()]     = asuint(mean);
    work[mltHeader() + 1] = 0;
    work[mltHeader() + 2] = 0;
    work[mltHeader() + 3] = 0;
    work[mltHeader() + 4] =
      asuint(sqrt(max(0, square / float(mltBootstrapCount()) - mean * mean) / float(mltBootstrapCount() - 1)));
    work[mltHeader() + 5] = positive;
}
// Bootstrap records live in work, not the material/light-distribution buffer.
uint mltSelectCDF(float u)
{
    uint lo = 0, hi = mltBootstrapCount();
    while (lo < hi)
    {
        uint mid = lo + (hi - lo) / 2;
        if (asfloat(work[mltBootstrapOffset() + mid * 2 + 1]) <= u)
            lo = mid + 1;
        else
            hi = mid;
    }
    return min(lo, mltBootstrapCount() - 1);
}
void mltTransition(inout uint rng, inout float3 value, inout float intensity, inout uint pixel, bool deposit)
{
    mltSeed  = randomBits(rng);
    mltMode  = 2;
    mltLarge = mltUniform(mltSeed, MLT_DIMENSIONS * 2 + 1) < asfloat(asuint(parameters[30]).w);
    uint   proposedPixel;
    float3 proposed  = mltEvaluate(proposedPixel);
    float  candidate = mltIntensity(proposed);
    float  accept    = intensity > 0 ? min(1, candidate / intensity) : 1;
    if (deposit)
    {
        mltSplat(pixel, value, intensity, 1 - accept);
        mltSplat(proposedPixel, proposed, candidate, accept);
    }
    bool accepted = mltUniform(mltSeed, MLT_DIMENSIONS * 2 + 2) < accept;
    if (accepted)
    {
        for (uint d = 0; d < MLT_DIMENSIONS; d++)
            work[mltAddress + d] = asuint(mltPrimary(d));
        value     = proposed;
        intensity = candidate;
        pixel     = proposedPixel;
    }
    if (deposit)
    {
        uint ignored;
        InterlockedAdd(work[mltHeader() + 2], 1, ignored);
        if (accepted)
            InterlockedAdd(work[mltHeader() + 1], 1, ignored);
    }
}
[numthreads(64, 1, 1)] void MLTMutate(uint3 id : SV_DispatchThreadID) {
    uint chain = id.x;
    if (chain >= mltChainCount())
        return;
    mltAddress    = mltChainOffset() + chain * (MLT_DIMENSIONS + 8);
    uint   record = mltAddress + MLT_DIMENSIONS, frame = asuint(parameters[24]).x;
    uint   rng;
    float3 value;
    float  intensity;
    uint   pixel;
    if (frame == 0)
    {
        rng         = mltHash(chain + asuint(parameters[14]).z * 0x1234567u + 0x95a827bu);
        float total = asfloat(work[mltHeader()]) * float(mltBootstrapCount());
        uint  index = mltSelectCDF(mltUniform(rng, 0) * total);
        mltMode     = 1;
        mltLarge    = true;
        mltSeed     = work[mltBootstrapOffset() + index * 2];
        for (uint d = 0; d < MLT_DIMENSIONS; d++)
            work[mltAddress + d] = asuint(mltUniform(mltSeed, d));
        value     = mltEvaluate(pixel);
        intensity = mltIntensity(value);
        for (uint burn = 0; burn < 16; burn++)
            mltTransition(rng, value, intensity, pixel, false);
    }
    else
    {
        value     = asfloat(uint3(work[record], work[record + 1], work[record + 2]));
        intensity = asfloat(work[record + 3]);
        pixel     = work[record + 4];
        rng       = work[record + 5];
    }
    uint count      = mltPixels() * asuint(parameters[25]).x;
    uint iterations = count / mltChainCount() + (chain < count % mltChainCount() ? 1 : 0);
    if (asfloat(work[mltHeader()]) > 0)
        for (uint j = 0; j < iterations; j++)
            mltTransition(rng, value, intensity, pixel, true);
    work[record]     = asuint(value.x);
    work[record + 1] = asuint(value.y);
    work[record + 2] = asuint(value.z);
    work[record + 3] = asuint(intensity);
    work[record + 4] = pixel;
    work[record + 5] = rng;
}

  [numthreads(8, 8, 1)] void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint4 frame = asuint(parameters[24]), options = asuint(parameters[25]);
    if (id.x >= frame.y || id.y >= frame.z)
        return;
    uint index = id.y * frame.y + id.x;
    if ((options.w & 1u) != 0)
    {
        float2 p         = (float2(id.xy) + .5) / float2(frame.yz) * 2 - 1;
        float3 origin, direction;
        cameraRay(p, origin, direction);
        Hit    hit;
        float4 guide = float4(0, 0, 0, -1);
        if (traceCamera(origin, direction, hit))
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
    bool   useMLT       = (options.w & 32u) != 0;
    uint   directOffset = mltPixels() * 10 + index * 4;
    float3 sum          = 0;
    // These modes allocate different scratch regions. A select expression may
    // speculatively load both operands; root UAVs have no bounds descriptor.
    [branch] if (frame.x != 0)
    {
        [branch] if (useMLT) sum = asfloat(uint3(work[directOffset], work[directOffset + 1], work[directOffset + 2]));
        else sum                 = accumulation[index].rgb;
    }
    for (uint sampleIndex = 0; sampleIndex < options.x; ++sampleIndex)
    {
        uint state = index * 1973u + (frame.x + sampleIndex) * 9277u + 89173u;
        randomBits(state);
        float2 p         = (float2(id.xy) + float2(random(state), random(state))) / float2(frame.yz) * 2 - 1;
        float3 origin, direction;
        cameraRay(p, origin, direction);
        if (useMLT)
        {
            sum += path(origin, direction, state, 1);
            continue;
        }
        bool   connectLightPaths = (options.w & 8u) != 0 && sourceGroupCount() > 0;
        bool   usePhotons        = (options.w & 16u) != 0 && sourceGroupCount() > 0;
        float3 value             = path(
          origin, direction, state, frame.w, !connectLightPaths && !usePhotons, 0, !connectLightPaths && !usePhotons);
        if (connectLightPaths)
            value += bidirectional(origin, direction, state, frame.w);
        if (usePhotons)
            value += photonCamera(origin, direction, state, frame.w);
        sum += max(value, 0);
    }
    if (useMLT)
    {
        work[directOffset]     = asuint(sum.x);
        work[directOffset + 1] = asuint(sum.y);
        work[directOffset + 2] = asuint(sum.z);
        uint at                = mltPixels() * 4 + index * 6;
        sum += float3(mltFilmChannel(at), mltFilmChannel(at + 2), mltFilmChannel(at + 4)) * asfloat(work[mltHeader()]);
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
[numthreads(8, 8, 1)] void DenoiseMain(uint3 id : SV_DispatchThreadID) {
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
