#ifdef NOVA_D3D12
cbuffer DrawOffsets : register(b1)
{
    uint2 drawOffsets;
};
#endif
struct Vertex
{
    float4 position;
    float4 normal;
    float4 uv;
};
[[vk::binding(0, 0)]] cbuffer Scene : register(b0)
{
    float4x4 viewProjection;
    float4   eyeExposure;
    float4   baseColorFactor;
    float4   materialFactor;
    float4   emissiveFactor;      // rgb: emissive factor, w: PBR enabled
    float4   lightDirection;      // xyz: normalized surface-to-light direction
    float4   lightColorIntensity; // rgb: linear color, w: intensity
    float4   cameraRight;
    float4   cameraUp;
    float4   cameraForward;     // w: environment loaded
    float4   environmentParams; // sin/cos rotation, IBL intensity, highest mip
    float4   skyParams;         // background intensity and visibility
    float4   irradiance[9];
};
[[vk::binding(1, 0)]] StructuredBuffer<Vertex> vertices : register(t0);
[[vk::binding(2, 0)]] Texture2D<float4>        baseTexture : register(t1);
[[vk::binding(3, 0)]] Texture2D<float4>        normalTexture : register(t2);
[[vk::binding(4, 0)]] Texture2D<float4>        materialTexture : register(t3);
[[vk::binding(5, 0)]] Texture2D<float4>        occlusionTexture : register(t4);
[[vk::binding(6, 0)]] Texture2D<float4>        emissiveTexture : register(t5);
[[vk::binding(7, 0)]] SamplerState             textureSampler : register(s0);
[[vk::binding(8, 0)]] TextureCube<float4>      environmentTexture : register(t6);
[[vk::binding(9, 0)]] TextureCube<float4>      backgroundTexture : register(t7);
[[vk::binding(10, 0)]] Texture2D<float4>       pathTexture : register(t8);
// Column-major matrices and immutable local-space vertices, shared by every backend.
struct SkinInput
{
    float4 position;
    float4 normal;
    uint4  joints;
    float4 weights;
};
struct JointMatrix
{
    float4 c0, c1, c2, c3;
};
[[vk::binding(11, 0)]] StructuredBuffer<SkinInput>   skinInputs : register(t9);
[[vk::binding(12, 0)]] StructuredBuffer<JointMatrix> jointMatrices : register(t10);
float4x4                                             jointMatrix(uint index)
{
    JointMatrix m = jointMatrices[index];
    return transpose(float4x4(m.c0, m.c1, m.c2, m.c3));
}
Vertex skinVertex(uint id)
{
    Vertex v = vertices[id];
    if (skyParams.w > .5)
    {
        SkinInput input = skinInputs[id];
        float4x4  m     = jointMatrix(input.joints.x) * input.weights.x + jointMatrix(input.joints.y) * input.weights.y
                     + jointMatrix(input.joints.z) * input.weights.z + jointMatrix(input.joints.w) * input.weights.w;
        v.position = float4(mul(m, input.position).xyz, v.position.w);
        float3 a   = float3(m[0][0], m[1][0], m[2][0]);
        float3 b   = float3(m[0][1], m[1][1], m[2][1]);
        float3 c   = float3(m[0][2], m[1][2], m[2][2]);
        float3 co0 = cross(b, c), co1 = cross(c, a), co2 = cross(a, b);
        float  det = dot(a, co0);
        float3 n   = co0 * input.normal.x + co1 * input.normal.y + co2 * input.normal.z;
        v.normal   = float4(abs(det) > 1e-8 ? normalize(n / det) : input.normal.xyz, 0);
    }
    return v;
}
// Validation readback uses the exact function called by the production vertex shader.
[[vk::binding(13, 0)]] RWStructuredBuffer<Vertex> skinOutput : register(u0);
[numthreads(64, 1, 1)] void                       SkinningCSMain(uint3 id : SV_DispatchThreadID) {
    uint count = uint(skyParams.z); // Validation dispatch count; unused by the vertex stage.
    if (id.x < count)
        skinOutput[id.x] = skinVertex(id.x);
}

struct InstanceData
{
    float4x4 world;
    float4x4 normalMatrix;
    float4   baseColor;
    float4   material;
    float4   emissive;
    float4   uv;
};
[[vk::binding(14, 0)]] StructuredBuffer<InstanceData> instances : register(t11);

float3 encodeDisplayRGB(float3 color);
float3 diagnosticLinear(float3 color);
float3 applyFog(float3 color, float3 position)
{
    if (irradiance[3].w < .5)
        return color;
    InstanceData globals = instances[uint(irradiance[3].w) - 1];
    float        mode    = globals.baseColor.w;
    if (mode < .5)
        return color;
    float  depth  = max(0, dot(position - eyeExposure.xyz, cameraForward.xyz));
    float  amount = mode < 1.5
                      ? saturate((depth - globals.material.x) / max(.0001, globals.material.y - globals.material.x))
                      : 1 - exp(-depth * depth * globals.material.z * globals.material.z);
    float3 fog    = globals.baseColor.rgb;
#if defined(NOVA_WEB_OUTPUT) && !defined(NOVA_RASTER_AA)
    // Decode the web presentation value before mixing, matching native sRGB render targets.
    float3 linearColor = float3(
      color.x <= .04045 ? color.x / 12.92 : pow((color.x + .055) / 1.055, 2.4),
      color.y <= .04045 ? color.y / 12.92 : pow((color.y + .055) / 1.055, 2.4),
      color.z <= .04045 ? color.z / 12.92 : pow((color.z + .055) / 1.055, 2.4));
    return encodeDisplayRGB(lerp(linearColor, fog, saturate(amount)));
#else
    if (irradiance[0].w > .5)
        fog = -log(max(1 - fog, float3(.00001, .00001, .00001)));
#endif
    return lerp(color, fog, saturate(amount));
}
bool shadowBox(float3 low, float3 high, float3 origin, float3 direction, float maxDistance)
{
    float nearT = 0, farT = maxDistance;
    [unroll] for (uint k = 0; k < 3; k++)
    {
        float pad = 1e-5 * max(1, max(abs(low[k]), abs(high[k])));
        if (abs(direction[k]) < 1e-15)
        {
            if (origin[k] < low[k] - pad || origin[k] > high[k] + pad)
                return false;
        }
        else
        {
            float a = (low[k] - pad - origin[k]) / direction[k], b = (high[k] + pad - origin[k]) / direction[k];
            nearT = max(nearT, min(a, b));
            farT  = min(farT, max(a, b));
            if (farT < nearT)
                return false;
        }
    }
    return true;
}
bool shadowTriangle(InstanceData tri, float3 origin, float3 direction, float maxDistance, uint materialBase)
{
    float3 a = tri.baseColor.xyz, e = tri.material.xyz - a, f = tri.emissive.xyz - a, p = cross(direction, f);
    float  determinant = dot(e, p);
    if (abs(determinant) < 1e-12)
        return false;
    float  inverse = 1 / determinant;
    float3 delta   = origin - a;
    float  u       = dot(delta, p) * inverse;
    if (u < 0 || u > 1)
        return false;
    float3 q = cross(delta, e);
    float  v = dot(direction, q) * inverse;
    float  distance = dot(f, q) * inverse;
    if (!(v >= 0 && u + v <= 1 && distance > 1e-5 && distance < maxDistance))
        return false;
    InstanceData material = instances[materialBase + uint(tri.emissive.w)];
    uint         flags = uint(material.emissive.w), mode = flags & 3;
    if (mode == 2 || ((flags & 4) == 0 && determinant <= 0))
        return false;
    if (mode == 1)
    {
        float2 uv =
          tri.uv.xy * (1 - u - v) + tri.uv.zw * u + float2(tri.normalMatrix[0][0], tri.normalMatrix[1][1]) * v;
        uv = frac(uv) * material.uv.xy + material.uv.zw;
        if (baseTexture.SampleLevel(textureSampler, uv, 0).a * material.baseColor.a < frac(material.emissive.w) * 4)
            return false;
    }
    return true;
}
bool sceneOccluded(uint header, float3 origin, float3 direction, float maxDistance)
{
    InstanceData settings = instances[header];
    [loop] for (uint i = 0; i < uint(settings.emissive.x); i++)
    {
        InstanceData instance = instances[i];
        if (instance.uv.z < .5)
            continue;
        // Explicit BVH transform components; the Web visibility kernel is optimized
        // to inline matrix storage access before SPIR-V/WGSL lowering.
        float3 translation = float3(instances[i].world._m03, instances[i].world._m13, instances[i].world._m23);
        float3 nx =
          float3(instances[i].normalMatrix._m00, instances[i].normalMatrix._m10, instances[i].normalMatrix._m20);
        float3 ny =
          float3(instances[i].normalMatrix._m01, instances[i].normalMatrix._m11, instances[i].normalMatrix._m21);
        float3 nz =
          float3(instances[i].normalMatrix._m02, instances[i].normalMatrix._m12, instances[i].normalMatrix._m22);
        float3   delta          = origin - translation;
        float3   localOrigin    = float3(dot(nx, delta), dot(ny, delta), dot(nz, delta));
        float3   localDirection = float3(dot(nx, direction), dot(ny, direction), dot(nz, direction));
        uint     root = header + 1 + uint(instance.uv.y), node = 0, count = uint(instance.uv.w);
        [loop] while (node < count)
        {
            InstanceData box = instances[root + node];
            if (!shadowBox(box.baseColor.xyz, box.material.xyz, localOrigin, localDirection, maxDistance))
            {
                node = uint(box.baseColor.w);
                continue;
            }
            uint triangles = uint(box.uv.w);
            if (triangles == 0)
            {
                node = uint(box.uv.x);
                continue;
            }
            [loop] for (uint t = 0; t < triangles; t++) if (shadowTriangle(
                                                              instances[root + uint(box.uv.z) + t],
                                                              localOrigin,
                                                              localDirection,
                                                              maxDistance,
                                                              uint(instance.uv.x))) return true;
            node = uint(box.baseColor.w);
        }
    }
    return false;
}
float sceneVisibility(float3 position, float3 normal, float3 light, float softness)
{
    if (irradiance[3].w < .5)
        return 1;
    uint         header   = uint(irradiance[3].w) - 1;
    InstanceData settings = instances[header];
    if ((irradiance[1].w > .5 ? irradiance[4].w < .5 : settings.emissive.y < .5) || settings.emissive.x < .5)
        return 1;
    light          = normalize(light);
    float3 axis    = abs(light.y) < .99 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tangent = normalize(cross(light, axis)), bitangent = cross(light, tangent);
    uint   count   = softness < .0001 ? 1 : uint(irradiance[1].w > .5 ? irradiance[2].w : settings.emissive.w);
    float  visible = 0;
    [loop] for (uint sampleIndex = 0; sampleIndex < count; sampleIndex++)
    {
        float  radius = count == 1 ? 0 : sqrt((float(sampleIndex) + .5) / count) * softness * .006;
        float  angle  = float(sampleIndex) * 2.39996323;
        float3 ray    = normalize(light + radius * (cos(angle) * tangent + sin(angle) * bitangent));
        visible += sceneOccluded(header, position + normalize(normal) * settings.uv.x, ray, 1e30) ? 0 : 1;
    }
    return visible / count;
}
// Finite shadow segment: local BVH directions are deliberately not normalized,
// so their intersection parameter retains world-space distance under nonuniform scale.
float localLightVisibility(float3 position, float3 normal, float3 lightPosition, float softness)
{
    if (irradiance[3].w < .5)
        return 1;
    uint         header   = uint(irradiance[3].w) - 1;
    InstanceData settings = instances[header];
    if ((irradiance[1].w > .5 ? irradiance[4].w < .5 : settings.emissive.y < .5) || settings.emissive.x < .5)
        return 1;
    float3 origin   = position + normalize(normal) * settings.uv.x;
    float3 delta    = lightPosition - origin;
    float  distance = length(delta);
    if (distance < 1e-5)
        return 1;
    float3 light   = delta / distance;
    float3 axis    = abs(light.y) < .99 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tangent = normalize(cross(light, axis)), bitangent = cross(light, tangent);
    uint   count   = softness < .0001 ? 1 : uint(irradiance[1].w > .5 ? irradiance[2].w : settings.emissive.w);
    float  visible = 0;
    [loop] for (uint i = 0; i < count; ++i)
    {
        // Deterministic disk around the source, radius in world units.
        float  radius         = count == 1 ? 0 : sqrt((float(i) + .5) / count) * softness * .006;
        float  angle          = float(i) * 2.39996323;
        float3 sampleDelta    = delta + radius * (cos(angle) * tangent + sin(angle) * bitangent);
        float  sampleDistance = length(sampleDelta);
        visible += sceneOccluded(header, origin, sampleDelta / sampleDistance, max(0, sampleDistance - 1e-5)) ? 0 : 1;
    }
    return visible / count;
}
struct VertexOut
{
    float4                position : SV_Position;
    float3                worldPosition : TEXCOORD0;
    float3                normal : TEXCOORD1;
    float2                uv : TEXCOORD2;
    nointerpolation float generatedNormal : TEXCOORD3;
    nointerpolation uint  instanceIndex : TEXCOORD4;
    nointerpolation float winding : TEXCOORD5;
};
VertexOut VSMain(uint id : SV_VertexID, uint instanceId : SV_InstanceID)
{
#ifdef NOVA_D3D12
    id += drawOffsets.x;
    instanceId += drawOffsets.y;
#endif
    Vertex    v = skinVertex(id);
    uint      materialIndex = uint(max(1, v.position.w)) - 1;
    v.position.w            = 1;
    if (lightDirection.w > .5 && lightDirection.w < 1.5)
    {
        InstanceData instance = instances[instanceId];
        materialIndex += uint(instance.uv.x);
        v.position            = mul(instance.world, v.position);
        v.normal.xyz          = mul((float3x3)instance.normalMatrix, v.normal.xyz);
    }
    VertexOut o;
    o.winding = 1;
    if (lightDirection.w > .5 && lightDirection.w < 1.5)
        o.winding = determinant((float3x3)instances[instanceId].world) < 0 ? -1 : 1;
    o.instanceIndex   = materialIndex;
    o.position        = mul(viewProjection, v.position);
    o.worldPosition   = v.position.xyz;
    o.normal          = v.normal.xyz;
    o.uv              = v.uv.xy;
    o.generatedNormal = v.uv.z;
    return o;
}
float3 fresnel(float cosine, float3 f0)
{
    return f0 + (1 - f0) * pow(1 - saturate(cosine), 5);
}
float3 directLight(float3 n, float3 v, float3 l, float3 base, float metallic, float roughness, float3 radiance)
{
    float3 halfVector = v + l;
    float3 h          = halfVector * rsqrt(max(dot(halfVector, halfVector), 1e-8));
    float  nv = max(dot(n, v), 0.001), nl = saturate(dot(n, l)), nh = saturate(dot(n, h));
    float  a = roughness * roughness, a2 = a * a;
    float  d = a2 / (3.14159265 * pow(nh * nh * (a2 - 1) + 1, 2) + 0.0001);
    float  k = pow(roughness + 1, 2) / 8;
    float  g = (nv / (nv * (1 - k) + k)) * (nl / (nl * (1 - k) + k));
    float3 f = fresnel(dot(v, h), lerp(float3(0.04, 0.04, 0.04), base, metallic));
    return ((1 - f) * (1 - metallic) * base / 3.14159265 + d * g * f / (4 * nv * max(nl, 0.001))) * radiance * nl;
}
// Analytic studio environment; no external HDR file is required by this example.
float3 studio(float3 r, float roughness)
{
    float3 sky      = lerp(float3(0.035, 0.04, 0.055), float3(0.45, 0.52, 0.65), saturate(r.y * 0.5 + 0.5));
    float  exponent = lerp(160, 2, roughness);
    sky += float3(4, 3.7, 3.3) * pow(saturate(dot(r, normalize(float3(-1, 1, 1)))), exponent);
    sky += float3(1.4, 1.8, 2.5) * pow(saturate(dot(r, normalize(float3(1, 0.4, -1)))), exponent);
    return sky;
}
float3 environmentDirection(float3 direction)
{
    float sine = environmentParams.x, cosine = environmentParams.y;
    return float3(cosine * direction.x + sine * direction.z, direction.y, -sine * direction.x + cosine * direction.z);
}
float3 diffuseEnvironment(float3 n)
{
    n              = environmentDirection(n);
    float basis[9] = {
      0.282095,
      0.488603 * n.y,
      0.488603 * n.z,
      0.488603 * n.x,
      1.092548 * n.x * n.y,
      1.092548 * n.y * n.z,
      0.315392 * (3 * n.z * n.z - 1),
      1.092548 * n.x * n.z,
      0.546274 * (n.x * n.x - n.y * n.y)};
    float3 result = 0;
    [unroll] for (int i = 0; i < 9; ++i) result += irradiance[i].rgb * basis[i];
    return max(result, 0);
}
// Analytical approximation to the split-sum BRDF integral (GGX prefilter is baked offline).
float2 environmentBRDF(float roughness, float nv)
{
    float4 r    = roughness * float4(-1, -0.0275, -0.572, 0.022) + float4(1, 0.0425, 1.04, -0.04);
    float  a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}
float3 encodeDisplayRGB(float3 c)
{
    c = max(c, 0);
    return float3(
      c.x <= .0031308 ? 12.92 * c.x : 1.055 * pow(c.x, 1 / 2.4) - .055,
      c.y <= .0031308 ? 12.92 * c.y : 1.055 * pow(c.y, 1 / 2.4) - .055,
      c.z <= .0031308 ? 12.92 * c.z : 1.055 * pow(c.z, 1 / 2.4) - .055);
}
float3 displayRadiance(float3 color)
{
    if (irradiance[0].w > .5)
        return max(color, 0) * eyeExposure.w;
    color = 1 - exp(-max(color, 0) * eyeExposure.w);
#if defined(NOVA_WEB_OUTPUT) && !defined(NOVA_RASTER_AA)
    color = encodeDisplayRGB(color);
#endif
    return color;
}
struct SkyOut
{
    float4 position : SV_Position;
    float3 direction : TEXCOORD0;
    float2 uv : TEXCOORD1;
};
SkyOut SkyVSMain(uint id : SV_VertexID)
{
    float2 positions[3] = {float2(-1, -1), float2(-1, 3), float2(3, -1)};
    float2 p            = positions[id];
    SkyOut output;
    output.position  = float4(p, 1, 1);
    output.uv        = float2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    output.direction =
      cameraForward.xyz
      + (cameraUp.w < 0 ? float3(0, 0, 0) : p.x * cameraRight.w * cameraRight.xyz + p.y * cameraUp.w * cameraUp.xyz);
    return output;
}
float4 SkyPSMain(SkyOut input)
  : SV_Target
{
    if (skyParams.z > 0.5)
    {
        uint width, height;
        pathTexture.GetDimensions(width, height);
        float2 halfTexel = 0.5 / float2(width, height);
        float3 color     = pathTexture.SampleLevel(textureSampler, clamp(input.uv, halfTexel, 1 - halfTexel), 0).rgb;
        return float4(skyParams.z > 1.5 ? diagnosticLinear(color) : color, 1);
    }
    float3 direction = environmentDirection(normalize(input.direction));
    float3 color     = backgroundTexture.Sample(textureSampler, direction).rgb * skyParams.x;
    return float4(displayRadiance(color), 1);
}
struct SurfaceData
{
    float3 position, normal, geometricNormal, base, emissive;
    float  metallic, roughness, ao;
    float  opacity, alphaInfo;
    float4 visibility0, visibility1, visibility2, visibility3;
    uint   useVisibility;
};
SurfaceData sampleSurface(VertexOut input)
{
    float  alphaInfo  = .125;
    float2 uv         = input.uv;
    float4 baseFactor = baseColorFactor, matFactor = materialFactor, emissionFactor = emissiveFactor;
    if (lightDirection.w > .5)
    {
        InstanceData instance = instances[input.instanceIndex];
        uv                    = frac(uv) * instance.uv.xy + instance.uv.zw;
        baseFactor            = instance.baseColor;
        matFactor             = instance.material;
        emissionFactor.rgb    = instance.emissive.rgb;
        alphaInfo             = instance.emissive.w;
    }
    float4 baseSample = baseTexture.Sample(textureSampler, uv);
    float3 base       = baseSample.rgb * baseFactor.rgb;
    float3 n   = normalize(input.normal);
    float3 dp1 = ddx(input.worldPosition), dp2 = ddy(input.worldPosition);
    if (input.generatedNormal > .5)
    {
        float3 face = cross(dp1, dp2);
        if (dot(face, face) > 1e-12)
        {
            face = normalize(face);
            n    = dot(face, n) < 0 ? -face : face;
        }
    }
    float2 duv1 = ddx(input.uv), duv2 = ddy(input.uv);
    float3 p2perp = cross(dp2, n), p1perp = cross(n, dp1);
    float3 t = p2perp * duv1.x + p1perp * duv2.x, b = p2perp * duv1.y + p1perp * duv2.y;
    float  scale   = rsqrt(max(max(dot(t, t), dot(b, b)), 0.00001));
    float3 sampled = normalTexture.Sample(textureSampler, uv).xyz * 2 - 1;
    sampled.xy *= matFactor.z;
    n               = normalize(t * scale * sampled.x + b * scale * sampled.y + n * sampled.z);
    float4 mr       = materialTexture.Sample(textureSampler, uv);
    float  metallic = saturate(mr.b * matFactor.x), roughness = clamp(mr.g * matFactor.y, 0.06, 1);
    float  ao = lerp(1, occlusionTexture.Sample(textureSampler, uv).r, matFactor.w);
    SurfaceData result;
    result.useVisibility = 0;
    result.visibility0 = result.visibility1 = result.visibility2 = result.visibility3 = 1;
    result.opacity         = baseSample.a * baseFactor.a;
    result.alphaInfo       = alphaInfo;
    result.position        = input.worldPosition;
    result.normal          = n;
    result.geometricNormal = normalize(input.normal);
    result.base            = base;
    result.metallic        = metallic;
    result.roughness       = roughness;
    result.ao              = ao;
    result.emissive        = emissiveTexture.Sample(textureSampler, uv).rgb * emissionFactor.rgb;
    return result;
}
float hybridVisibility(SurfaceData s, uint index)
{
    return index < 4    ? s.visibility0[index]
           : index < 8  ? s.visibility1[index - 4]
           : index < 12 ? s.visibility2[index - 8]
                        : s.visibility3[min(index - 12, 3)];
}
float3 surfaceRadiance(SurfaceData s, float3 raySpecular, float reflectionMix, float3 viewDirection)
{
    float3 base = s.base;
    if (emissiveFactor.w < .5)
        return base;
    float3 n = s.normal, v = viewDirection;
    float  metallic = s.metallic, roughness = s.roughness, ao = s.ao;
    bool   collection = false;
    uint   header     = 0;
    if (irradiance[3].w > .5)
    {
        header     = uint(irradiance[3].w) - 1;
        collection = instances[header].uv.w > .5;
    }
    float3 color = 0;
    if (collection)
    {
        uint count = uint(instances[header].uv.y), offset = uint(instances[header].uv.z);
        [loop] for (uint i = 0; i < count; ++i)
        {
            InstanceData source      = instances[offset + i];
            uint         type        = uint(source.emissive.w);
            float3       l           = source.emissive.xyz;
            float        attenuation = 1;
            if (type != 0)
            {
                float3 delta           = source.material.xyz - s.position;
                float  distanceSquared = dot(delta, delta);
                float  distance        = sqrt(distanceSquared);
                l                      = distance > .000001 ? delta / distance : float3(0, 1, 0);
                attenuation            = 1 / max(distanceSquared, .01);
                if (source.material.w > 0)
                    attenuation *= pow(saturate(1 - pow(distance / source.material.w, 4)), 2);
                if (type == 2)
                {
                    float cone = dot(-l, source.emissive.xyz);
                    attenuation *= source.uv.x > source.uv.y + .000001 ? smoothstep(source.uv.y, source.uv.x, cone)
                                                                       : step(source.uv.y, cone);
                }
            }
            if (attenuation <= 0)
                continue;
            float visibility = 1;
            if (s.useVisibility)
                visibility = hybridVisibility(s, i);
            else if (source.uv.z > .5)
                visibility = type == 0
                               ? sceneVisibility(s.position, s.geometricNormal, l, source.uv.w)
                               : localLightVisibility(s.position, s.geometricNormal, source.material.xyz, source.uv.w);
            color += directLight(
              n, v, l, base, metallic, roughness, source.baseColor.rgb * source.baseColor.w * attenuation * visibility);
        }
    }
    else
        color = directLight(
          n,
          v,
          lightDirection.xyz,
          base,
          metallic,
          roughness,
          lightColorIntensity.rgb * lightColorIntensity.w
            * (s.useVisibility?s.visibility0.x:sceneVisibility(
              s.position,
              s.geometricNormal,
              lightDirection.xyz,
              irradiance[3].w > .5 ? instances[uint(irradiance[3].w) - 1].emissive.z : 0)));
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), base, metallic);
    float3 f  = fresnel(saturate(dot(n, v)), f0);
    if (cameraForward.w > 0.5)
    {
        float3 reflected = environmentDirection(reflect(-v, n));
        float3 prefiltered =
          environmentTexture.SampleLevel(textureSampler, reflected, roughness * environmentParams.w).rgb;
        float2 brdf    = environmentBRDF(roughness, saturate(dot(n, v)));
        float3 ambient = (1 - f) * (1 - metallic) * base * diffuseEnvironment(n) * environmentParams.z
                         + lerp(prefiltered * (f0 * brdf.x + brdf.y) * environmentParams.z, raySpecular, reflectionMix);
        color += ambient * ao;
    }
    else
    {
        if (!collection)
            color += directLight(n, v, normalize(float3(1, 0.7, -1)), base, metallic, roughness, float3(1, 1.6, 2.7));
        color += (base * (1 - metallic) * studio(n, 1) * 0.4
                  + lerp(studio(reflect(-v, n), roughness) * f, raySpecular, reflectionMix))
                 * ao;
    }
    color += s.emissive;
    return color;
}
float4 shadeSurface(SurfaceData s)
{
    float3 base = s.base;
    if (emissiveFactor.w < .5)
    {
#if defined(NOVA_WEB_OUTPUT) && !defined(NOVA_RASTER_AA)
        base = encodeDisplayRGB(base);
#endif
        return float4(applyFog(base, s.position), 1);
    }
    return float4(
      applyFog(
        displayRadiance(
          surfaceRadiance(s, 0, 0, cameraUp.w < 0 ? -cameraForward.xyz : normalize(eyeExposure.xyz - s.position))),
        s.position),
      1);
}

void materialCoverage(inout SurfaceData s, bool face, float winding, bool transparent)
{
    bool front = winding < 0 ? !face : face;
    uint flags = (uint)s.alphaInfo, mode = flags & 3;
    if ((flags & 4) == 0 && !front)
        discard;
    if (mode == 1 && s.opacity < frac(s.alphaInfo) * 4)
        discard;
    if (transparent ? (mode != 2 || s.opacity <= 0) : mode == 2)
        discard;
    if (!front)
    {
        s.normal          = -s.normal;
        s.geometricNormal = -s.geometricNormal;
    }
}
float4 PSMain(VertexOut input, bool face : SV_IsFrontFace)
  : SV_Target
{
    SurfaceData s = sampleSurface(input);
    materialCoverage(s, face, input.winding, false);
    return shadeSurface(s);
}
struct OITOutput
{
    float4 accumulation : SV_Target0;
    float4 revealage : SV_Target1;
};
OITOutput OITPSMain(VertexOut input, bool face : SV_IsFrontFace)
{
    SurfaceData s = sampleSurface(input);
    materialCoverage(s, face, input.winding, true);
    float     depth  = max(0, dot(s.position - eyeExposure.xyz, cameraForward.xyz));
    float     weight = clamp(1 / (1 + .01 * depth * depth), .01, 1);
    OITOutput o;
    o.accumulation = float4(shadeSurface(s).rgb * s.opacity * weight, s.opacity * weight);
    o.revealage    = s.opacity.xxxx;
    return o;
}

// Four 8-byte targets fit WebGPU's baseline 32-byte attachment budget.
// Both normals use octahedral encoding; roughness > 0 marks a covered pixel.
float2 octEncode(float3 n)
{
    n /= max(abs(n.x) + abs(n.y) + abs(n.z), 1e-8);
    float2 r = n.xy;
    if (n.z < 0)
        r = (1 - abs(r.yx)) * float2(r.x >= 0 ? 1 : -1, r.y >= 0 ? 1 : -1);
    return r;
}
float3 octDecode(float2 e)
{
    float3 n = float3(e, 1 - abs(e.x) - abs(e.y));
    float  t = saturate(-n.z);
    n.xy += float2(n.x >= 0 ? -t : t, n.y >= 0 ? -t : t);
    return normalize(n);
}
struct ModelGBuffer
{
    float4 baseRoughness : SV_Target0;
    float4 normals : SV_Target1;
    float4 positionAO : SV_Target2;
    float4 emissiveMetallic : SV_Target3;
};
ModelGBuffer GBufferPSMain(VertexOut input, bool face : SV_IsFrontFace)
{
    SurfaceData  s = sampleSurface(input);
    materialCoverage(s, face, input.winding, false);
    ModelGBuffer o;
    o.baseRoughness    = float4(s.base, s.roughness);
    o.normals          = float4(octEncode(s.normal), octEncode(s.geometricNormal));
    // Split logarithmic view depth avoids half-float world-position quantization.
    // Both halves survive RGBA16F storage, preserving near/far scale without an extra target.
    float depth        = log2(max(1e-20, dot(s.position - eyeExposure.xyz, cameraForward.xyz)));
    float high         = f16tof32(f32tof16(depth));
    o.positionAO       = float4(high, depth - high, s.ao, 0);
    o.emissiveMetallic = float4(s.emissive, s.metallic);
    return o;
}
[[vk::binding(17, 1)]] Texture2D<float4> gBase : register(t17);
[[vk::binding(18, 1)]] Texture2D<float4> gNormals : register(t18);
[[vk::binding(19, 1)]] Texture2D<float4> gPosition : register(t19);
[[vk::binding(20, 1)]] Texture2D<float4> gEmission : register(t20);
[[vk::binding(21, 1)]] Texture2D<float4> hybridShadow0 : register(t21);
[[vk::binding(22, 1)]] Texture2D<float4> hybridShadow1 : register(t22);
[[vk::binding(23, 1)]] Texture2D<float4> hybridShadow2 : register(t23);
[[vk::binding(24, 1)]] Texture2D<float4> hybridShadow3 : register(t24);
float3                                   surfacePosition(SkyOut input, float2 encodedDepth)
{
    float depth = exp2(encodedDepth.x + encodedDepth.y);
    if (cameraUp.w >= 0)
        return eyeExposure.xyz + input.direction * depth;
    float2 ndc = float2(input.uv.x * 2 - 1, 1 - input.uv.y * 2);
    return eyeExposure.xyz + cameraForward.xyz * depth + cameraRight.xyz * (ndc.x * cameraRight.w)
           + cameraUp.xyz * (ndc.y * abs(cameraUp.w));
}
// Sixteen independent light visibilities; no single scalar shadow factor is shared across lights.
ModelGBuffer HybridShadowPSMain(SkyOut input)
{
    int3         pixel = int3(int2(input.position.xy), 0);
    ModelGBuffer output;
    float        visibility[16];
    [unroll] for (uint j = 0; j < 16; j++) visibility[j] = 1;
    if (gBase.Load(pixel).w > 0 && irradiance[4].w > .5 && irradiance[3].w > .5)
    {
        float3 position = surfacePosition(input, gPosition.Load(pixel).xy), normal = octDecode(gNormals.Load(pixel).zw);
        uint   header        = uint(irradiance[3].w) - 1;
        InstanceData globals = instances[header];
        if (globals.uv.w > .5)
        {
            uint count = min(16, uint(globals.uv.y)), offset = uint(globals.uv.z);
            [loop] for (uint i = 0; i < count; i++)
            {
                InstanceData light = instances[offset + i];
                if (light.uv.z > .5 && light.baseColor.w > 0)
                    visibility[i] = uint(light.emissive.w) == 0
                                      ? sceneVisibility(position, normal, light.emissive.xyz, light.uv.w)
                                      : localLightVisibility(position, normal, light.material.xyz, light.uv.w);
            }
        }
        else
            visibility[0] = sceneVisibility(position, normal, lightDirection.xyz, globals.emissive.z);
    }
    output.baseRoughness    = float4(visibility[0], visibility[1], visibility[2], visibility[3]);
    output.normals          = float4(visibility[4], visibility[5], visibility[6], visibility[7]);
    output.positionAO       = float4(visibility[8], visibility[9], visibility[10], visibility[11]);
    output.emissiveMetallic = float4(visibility[12], visibility[13], visibility[14], visibility[15]);
    return output;
}
float3                                   diagnosticLinear(float3 c)
{
    c = saturate(c);
    return float3(
      c.x <= .04045 ? c.x / 12.92 : pow((c.x + .055) / 1.055, 2.4),
      c.y <= .04045 ? c.y / 12.92 : pow((c.y + .055) / 1.055, 2.4),
      c.z <= .04045 ? c.z / 12.92 : pow((c.z + .055) / 1.055, 2.4));
}
// Cosine-weighted finite hemisphere visibility. AO only attenuates indirect lighting.
float hybridAO(float3 position, float3 normal)
{
    if (irradiance[3].w < .5)
        return 1;
    uint  header = uint(irradiance[3].w) - 1;
    float radius = instances[header].world._m00;
    if (radius <= 0)
        return 1;
    uint   samples   = uint(instances[header].world._m10);
    float  strength  = instances[header].world._m20;
    float3 tangent   = normalize(cross(abs(normal.y) < .99 ? float3(0, 1, 0) : float3(1, 0, 0), normal));
    float3 bitangent = cross(normal, tangent);
    float  visible   = 0;
    float3 origin    = position + normal * max(1e-4, instances[header].uv.x);
    [loop] for (uint i = 0; i < samples; ++i)
    {
        float  r = sqrt((i + .5) / samples), phi = i * 2.39996323;
        float3 direction = tangent * (r * cos(phi)) + bitangent * (r * sin(phi)) + normal * sqrt(1 - r * r);
        visible += sceneOccluded(header, origin, direction, radius) ? 0 : 1;
    }
    return pow(saturate(visible / max(1, samples)), strength);
}
// Nearest-hit traversal uses world-distance parameters even for scaled instances.
// BLEND is excluded, MASK and sidedness use the same coverage as shadow rays.
bool hybridNearest(
  uint        header,
  float3      origin,
  float3      direction,
  inout float distance,
  out uint    hitInstance,
  out uint    hitTriangle,
  out float2  hitBary)
{
    bool found  = false;
    hitInstance = hitTriangle = 0;
    hitBary                   = 0;
    [loop] for (uint i = 0; i < uint(instances[header].emissive.x); ++i)
    {
        float3 translation = float3(instances[i].world._m03, instances[i].world._m13, instances[i].world._m23);
        float3 nx =
          float3(instances[i].normalMatrix._m00, instances[i].normalMatrix._m10, instances[i].normalMatrix._m20);
        float3 ny =
          float3(instances[i].normalMatrix._m01, instances[i].normalMatrix._m11, instances[i].normalMatrix._m21);
        float3 nz =
          float3(instances[i].normalMatrix._m02, instances[i].normalMatrix._m12, instances[i].normalMatrix._m22);
        float3 delta = origin - translation;
        float3 o     = float3(dot(nx, delta), dot(ny, delta), dot(nz, delta));
        float3 d     = float3(dot(nx, direction), dot(ny, direction), dot(nz, direction));
        uint   root = header + 1 + uint(instances[i].uv.y), node = 0, count = uint(instances[i].uv.w);
        [loop] while (node < count)
        {
            InstanceData box = instances[root + node];
            if (!shadowBox(box.baseColor.xyz, box.material.xyz, o, d, distance))
            {
                node = uint(box.baseColor.w);
                continue;
            }
            uint triangles = uint(box.uv.w);
            if (triangles == 0)
            {
                node = uint(box.uv.x);
                continue;
            }
            [loop] for (uint t = 0; t < triangles; ++t)
            {
                uint         index = root + uint(box.uv.z) + t;
                InstanceData tri   = instances[index];
                if (!shadowTriangle(tri, o, d, distance, uint(instances[i].uv.x)))
                    continue;
                float3 e = tri.material.xyz - tri.baseColor.xyz, f = tri.emissive.xyz - tri.baseColor.xyz;
                float3 p = cross(d, f), q = cross(o - tri.baseColor.xyz, e);
                float  inverse = 1 / dot(e, p);
                distance       = dot(f, q) * inverse;
                hitBary        = float2(dot(o - tri.baseColor.xyz, p), dot(d, q)) * inverse;
                hitInstance    = i;
                hitTriangle    = index;
                found          = true;
            }
            node = uint(box.baseColor.w);
        }
    }
    return found;
}
float3 hybridReflection(SurfaceData primary)
{
    uint   header      = uint(irradiance[3].w) - 1;
    float  maxDistance = instances[header].world._m01;
    uint   samples     = uint(instances[header].world._m11);
    float3 n = primary.normal, v = cameraUp.w < 0 ? -cameraForward.xyz : normalize(eyeExposure.xyz - primary.position);
    float3 tangent   = normalize(cross(abs(n.y) < .99 ? float3(0, 1, 0) : float3(1, 0, 0), n)),
           bitangent = cross(n, tangent);
    float  nv = max(.001, dot(n, v)), rough = max(.02, primary.roughness), a = rough * rough;
    float3 sum = 0;
    [loop] for (uint j = 0; j < samples; ++j)
    {
        float  u = (j + .5) / samples, phi = j * 2.39996323;
        float  cosTheta = sqrt((1 - u) / (1 + (a * a - 1) * u)), sinTheta = sqrt(max(0, 1 - cosTheta * cosTheta));
        float3 h   = normalize(tangent * (sinTheta * cos(phi)) + bitangent * (sinTheta * sin(phi)) + n * cosTheta);
        float3 ray = reflect(-v, h);
        float  nl = dot(n, ray), nh = max(.001, dot(n, h)), vh = max(0, dot(v, h));
        if (nl <= 0)
            continue;
        float  distance = maxDistance;
        uint   instance, hitTri;
        float2 bary;
        float3 origin = primary.position + primary.geometricNormal * max(1e-4, instances[header].uv.x);
        float3 radiance;
        if (hybridNearest(header, origin, ray, distance, instance, hitTri, bary))
        {
            InstanceData tri = instances[hitTri];
            InstanceData mat = instances[uint(instances[instance].uv.x) + uint(tri.emissive.w)];
            float2       uv  = tri.uv.xy * (1 - bary.x - bary.y) + tri.uv.zw * bary.x
                        + float2(tri.normalMatrix[0][0], tri.normalMatrix[1][1]) * bary.y;
            uv                 = frac(uv) * mat.uv.xy + mat.uv.zw;
            float3 localNormal = cross(tri.material.xyz - tri.baseColor.xyz, tri.emissive.xyz - tri.baseColor.xyz);
            float3 worldNormal = normalize(mul((float3x3)instances[instance].normalMatrix, localNormal));
            if (dot(worldNormal, ray) > 0)
                worldNormal = -worldNormal;
            SurfaceData hit = (SurfaceData)0;
            hit.position    = origin + ray * distance;
            hit.normal = hit.geometricNormal = worldNormal;
            hit.base                         = baseTexture.SampleLevel(textureSampler, uv, 0).rgb * mat.baseColor.rgb;
            float4 mr                        = materialTexture.SampleLevel(textureSampler, uv, 0);
            hit.metallic                     = saturate(mr.b * mat.material.x);
            hit.roughness                    = clamp(mr.g * mat.material.y, .04, 1);
            hit.ao                           = 1;
            hit.emissive = emissiveTexture.SampleLevel(textureSampler, uv, 0).rgb * mat.emissive.rgb;
            radiance     = surfaceRadiance(hit, 0, 0, -ray);
        }
        else
            radiance =
              cameraForward.w > .5
                ? environmentTexture.SampleLevel(textureSampler, environmentDirection(ray), 0).rgb * environmentParams.z
                : studio(ray, 0);
        float k = pow(rough + 1, 2) / 8;
        float g = (nv / (nv * (1 - k) + k)) * (nl / (nl * (1 - k) + k));
        sum += radiance * fresnel(vh, lerp(.04.xxx, primary.base, primary.metallic)) * (g * vh / (nv * nh));
    }
    return sum / max(1, samples);
}

float4 DeferredPSMain(SkyOut input)
  : SV_Target
{
    float3 backgroundDirection = environmentDirection(normalize(input.direction));
    float3 backgroundDX = ddx(backgroundDirection), backgroundDY = ddy(backgroundDirection);
    int3   pixel = int3(int2(input.position.xy), 0);
    float4 base  = gBase.Load(pixel);
    uint   debug = uint(irradiance[8].w);
    if (base.w <= 0)
    {
        if (debug != 0 || (irradiance[1].w > .5 && irradiance[5].w > .5))
            return float4(0, 0, 0, 1);
        if (cameraForward.w > .5 && skyParams.y > .5)
            return float4(
              displayRadiance(
                backgroundTexture.SampleGrad(textureSampler, backgroundDirection, backgroundDX, backgroundDY).rgb
                * skyParams.x),
              1);
        discard;
    }
    float4      normals = gNormals.Load(pixel), pos = gPosition.Load(pixel), emission = gEmission.Load(pixel);
    SurfaceData s;
    s.useVisibility = irradiance[1].w > .5 ? 1 : 0;
    s.visibility0 = s.visibility1 = s.visibility2 = s.visibility3 = 1;
    if (s.useVisibility)
    {
        s.visibility0 = hybridShadow0.Load(pixel);
        s.visibility1 = hybridShadow1.Load(pixel);
        s.visibility2 = hybridShadow2.Load(pixel);
        s.visibility3 = hybridShadow3.Load(pixel);
    }
    if (s.useVisibility && irradiance[5].w > .5)
        return float4(diagnosticLinear(hybridVisibility(s, uint(irradiance[5].w) - 1).xxx), 1);
    s.base            = base.rgb;
    s.roughness       = base.w;
    s.normal          = octDecode(normals.xy);
    s.geometricNormal = octDecode(normals.zw);
    s.position        = surfacePosition(input, pos.xy);
    s.ao              = pos.z;
    s.emissive        = emission.rgb;
    s.metallic        = emission.w;
    if (s.useVisibility)
    {
        float ao = hybridAO(s.position, s.geometricNormal);
        if (irradiance[3].w > .5 && abs(instances[uint(irradiance[3].w) - 1].world._m30 - 2) < .1)
            return float4(diagnosticLinear(ao.xxx), 1);
        s.ao *= ao;
    }
    if (s.useVisibility && irradiance[3].w > .5 && instances[uint(irradiance[3].w) - 1].world._m01 > 0)
    {
        float3 reflection = hybridReflection(s);
        if (instances[uint(irradiance[3].w) - 1].world._m30 > 2.5)
            return float4(displayRadiance(reflection), 1);
        if (debug == 0)
            return float4(
              applyFog(
                displayRadiance(surfaceRadiance(
                  s,
                  reflection,
                  instances[uint(irradiance[3].w) - 1].world._m21,
                  cameraUp.w < 0 ? -cameraForward.xyz : normalize(eyeExposure.xyz - s.position))),
                s.position),
              1);
    }
    if (debug == 0)
        return shadeSurface(s);
    float3 value = 0;
    float  range = max(.001, irradiance[7].w);
    if (debug == 1)
        value = s.base;
    if (debug == 2)
        value = s.normal * .5 + .5;
    if (debug == 3)
        value = s.geometricNormal * .5 + .5;
    if (debug == 4)
        value = (max(0, dot(s.position - eyeExposure.xyz, cameraForward.xyz)) / range).xxx;
    if (debug == 5)
        value = s.roughness.xxx;
    if (debug == 6)
        value = s.metallic.xxx;
    if (debug == 7)
        value = s.ao.xxx;
    if (debug == 8)
        value = s.emissive;
    if (debug == 9)
        value = s.position / range * .5 + .5;
    return float4(diagnosticLinear(value), 1);
}

// TAA uses the previous camera and skin palette, not a screen-space history blend.
[[vk::binding(15, 0)]] cbuffer PreviousScene : register(b15)
{
    float4x4 previousViewProjection;
    float4   previousSceneRest[20];
}
[[vk::binding(16, 0)]] StructuredBuffer<JointMatrix> previousJoints : register(t16);
struct MotionOut
{
    float4 position : SV_Position;
    float4 current : TEXCOORD0;
    float4 previous : TEXCOORD1;
    float2                uv : TEXCOORD2;
    nointerpolation uint  materialIndex : TEXCOORD3;
    nointerpolation float winding : TEXCOORD4;
};
MotionOut MotionVSMain(uint id : SV_VertexID, uint instanceId : SV_InstanceID)
{
#ifdef NOVA_D3D12
    id += drawOffsets.x;
    instanceId += drawOffsets.y;
#endif
    Vertex current = skinVertex(id), old = vertices[id];
    if (skyParams.w > .5)
    {
        SkinInput input    = skinInputs[id];
        float4    position = 0;
        [unroll] for (uint j = 0; j < 4; j++)
        {
            JointMatrix m      = previousJoints[input.joints[j]];
            float4x4    matrix = transpose(float4x4(m.c0, m.c1, m.c2, m.c3));
            position += mul(matrix, input.position) * input.weights[j];
        }
        old.position = float4(position.xyz, 1);
    }
    uint  materialIndex = uint(max(1, current.position.w)) - 1;
    float winding       = 1;
    current.position.w = old.position.w = 1;
    if (lightDirection.w > .5 && lightDirection.w < 1.5)
    {
        materialIndex += uint(instances[instanceId].uv.x);
        winding          = determinant((float3x3)instances[instanceId].world) < 0 ? -1 : 1;
        current.position = mul(instances[instanceId].world, current.position);
        old.position     = mul(instances[instanceId].world, old.position);
    }
    MotionOut o;
    o.uv            = current.uv.xy;
    o.materialIndex = materialIndex;
    o.winding       = winding;
    o.current  = mul(viewProjection, current.position);
    o.previous = mul(previousViewProjection, old.position);
    o.position = o.current;
    return o;
}
float4 MotionPSMain(MotionOut input, bool face : SV_IsFrontFace)
  : SV_Target
{
    float2 uv     = input.uv;
    float4 factor = baseColorFactor;
    float  info   = .125;
    if (lightDirection.w > .5)
    {
        InstanceData m = instances[input.materialIndex];
        uv             = frac(uv) * m.uv.xy + m.uv.zw;
        factor         = m.baseColor;
        info           = m.emissive.w;
    }
    float opacity = baseTexture.Sample(textureSampler, uv).a * factor.a;
    uint  flags = uint(info), mode = flags & 3;
    bool  front = input.winding < 0 ? !face : face;
    if (((flags & 4) == 0 && !front) || mode == 2 || (mode == 1 && opacity < frac(info) * 4))
        discard;
    float2 current = input.current.xy / input.current.w * float2(.5, -.5) + .5,
           old     = input.previous.xy / input.previous.w * float2(.5, -.5) + .5;
    return float4(old - current, input.current.w / 16, .125);
}
float4 SkyMotionPSMain(SkyOut input)
  : SV_Target
{
    float4 previous = mul(previousViewProjection, float4(input.direction, 0));
    float2 uv       = previous.xy / max(previous.w, .00001) * float2(.5, -.5) + .5;
    return float4(uv - input.uv, 1, 1);
}
