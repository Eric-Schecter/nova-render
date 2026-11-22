#include "material-sheen.hlsli"
#include "material-iridescence.hlsli"
#include "material-anisotropy.hlsli"
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
    float4 uv1;
    float4 color;
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
[[vk::binding(28, 0)]] Texture2D<float4> coatStrengthTexture : register(t28);
[[vk::binding(29, 0)]] Texture2D<float4> coatRoughnessTexture : register(t29);
[[vk::binding(30, 0)]] Texture2D<float4> coatNormalTexture : register(t30);
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
Vertex skinVertex(uint id, uint paletteOffset = 0, bool animate = true)
{
    Vertex v = vertices[id];
    if (skyParams.w > .5 && animate)
    {
        SkinInput input = skinInputs[id];
        if (input.normal.w > 0)
        {
            uint first = uint(input.normal.w) - 1;
            uint count = skinInputs[first].joints.z;
            for (uint i = 0; i < count; ++i)
            {
                SkinInput delta = skinInputs[first + i];
                uint component = delta.joints.y;
                float weight = jointMatrix(paletteOffset + delta.joints.x)[component % 4][component / 4];
                input.position.xyz += weight * delta.position.xyz;
                input.normal.xyz += weight * delta.normal.xyz;
            }
        }
        float4x4  m     = jointMatrix(paletteOffset + input.joints.x) * input.weights.x
                     + jointMatrix(paletteOffset + input.joints.y) * input.weights.y
                     + jointMatrix(paletteOffset + input.joints.z) * input.weights.z
                     + jointMatrix(paletteOffset + input.joints.w) * input.weights.w;
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
float wrapBaseAxis(float uv, uint mode)
{
    return mode == 1 ? saturate(uv) : mode == 2 ? 1 - abs(frac(uv * .5) * 2 - 1) : frac(uv);
}
float2 wrapBaseUV(float2 uv, float info)
{
    uint modes = uint(info) >> 5;
    return float2(wrapBaseAxis(uv.x, modes & 3u), wrapBaseAxis(uv.y, (modes >> 2) & 3u));
}
float4 triangleColor(InstanceData tri, float2 bary)
{
    const float4 a = float4(tri.world._m02, tri.world._m12, tri.world._m22, tri.world._m32);
    const float4 b = float4(tri.world._m03, tri.world._m13, tri.world._m23, tri.world._m33);
    const float4 c = float4(tri.normalMatrix._m02, tri.normalMatrix._m12,
                            tri.normalMatrix._m22, tri.normalMatrix._m32);
    return a * (1 - bary.x - bary.y) + b * bary.x + c * bary.y;
}
bool shadowBox(float3 low, float3 high, float3 origin, float3 inverseDirection, bool3 parallel, float maxDistance)
{
    float3 pad = 1e-5 * max(1, max(abs(low), abs(high)));
    float3 lower = low - pad - origin, upper = high + pad - origin;
    bool3 outside = max(lower, -upper) > 0;
    if ((parallel.x && outside.x) || (parallel.y && outside.y) || (parallel.z && outside.z))
        return false;
    float3 a = lower * inverseDirection, b = upper * inverseDirection;
    float3 nearT = min(a, b), farT = max(a, b);
    nearT = float3(parallel.x ? 0 : nearT.x, parallel.y ? 0 : nearT.y, parallel.z ? 0 : nearT.z);
    farT = float3(parallel.x ? maxDistance : farT.x, parallel.y ? maxDistance : farT.y, parallel.z ? maxDistance : farT.z);
    return max(0, max(nearT.x, max(nearT.y, nearT.z))) <= min(maxDistance, min(farT.x, min(farT.y, farT.z)));
}
bool shadowBox(float3 low, float3 high, float3 origin, float3 direction, float maxDistance)
{
    bool3 parallel = abs(direction) < 1e-15;
    float3 inverseDirection = 1 / float3(parallel.x ? 1 : direction.x,
                                        parallel.y ? 1 : direction.y,
                                        parallel.z ? 1 : direction.z);
    return shadowBox(low, high, origin, inverseDirection, parallel, maxDistance);
}
bool intersectSceneTriangle(InstanceData tri, float3 origin, float3 direction, float maxDistance, uint materialBase,
                            bool shadowOnly, out float distance, out float2 bary)
{
    distance = maxDistance;
    bary = 0;
    float3 a = tri.baseColor.xyz, e = tri.material.xyz - a, f = tri.emissive.xyz - a, p = cross(direction, f);
    float  determinant = dot(e, p);
    if (abs(determinant) < 1e-12)
        return false;
    float  inverse = 1 / determinant;
    float3 delta   = origin - a;
    float  u       = dot(delta, p) * inverse;
    if (u < 0 || u > 1)
        return false;
    float3 q        = cross(delta, e);
    float  v        = dot(direction, q) * inverse;
    distance        = dot(f, q) * inverse;
    if (!(v >= 0 && u + v <= 1 && distance > 1e-5 && distance < maxDistance))
        return false;
    InstanceData material = instances[materialBase + uint(tri.emissive.w)];
    uint         flags = uint(material.emissive.w), mode = flags & 3;
    // Fully transmitting dielectrics should not block opaque visibility rays.
    if (shadowOnly && tri.world._m21 >= 1 && material.material.x == 0)
        return false;
    if (mode == 2 || ((flags & 4) == 0 && determinant <= 0))
        return false;
    if (mode == 1)
    {
        float2 uv =
          tri.uv.xy * (1 - u - v) + tri.uv.zw * u + float2(tri.normalMatrix[0][0], tri.normalMatrix[1][1]) * v;
        uv = wrapBaseUV(uv, material.emissive.w) * material.uv.xy + material.uv.zw;
        if (baseTexture.SampleLevel(textureSampler, uv, 0).a * material.baseColor.a * triangleColor(tri, float2(u, v)).a < frac(material.emissive.w) * 4)
            return false;
    }
    bary = float2(u, v);
    return true;
}
bool shadowTriangle(InstanceData tri, float3 origin, float3 direction, float maxDistance, uint materialBase, bool shadowOnly)
{
    float distance;
    float2 bary;
    return intersectSceneTriangle(tri, origin, direction, maxDistance, materialBase, shadowOnly, distance, bary);
}
bool sceneOccluded(uint header, float3 origin, float3 direction, float maxDistance, bool shadowOnly = true)
{
    InstanceData settings = instances[header];
    [loop] for (uint i = 0; i < uint(settings.emissive.x); i++)
    {
        InstanceData instance = instances[i];
        if (shadowOnly && instance.uv.z < .5)
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
        float3 delta          = origin - translation;
        float3 localOrigin    = float3(dot(nx, delta), dot(ny, delta), dot(nz, delta));
        float3 localDirection = float3(dot(nx, direction), dot(ny, direction), dot(nz, direction));
        bool3  parallel       = abs(localDirection) < 1e-15;
        float3 inverseDirection = 1 / float3(parallel.x ? 1 : localDirection.x,
                                            parallel.y ? 1 : localDirection.y,
                                            parallel.z ? 1 : localDirection.z);
        uint   root = header + 1 + uint(instance.uv.y), node = 0, count = uint(instance.uv.w);
        [loop] while (node < count)
        {
            InstanceData box = instances[root + node];
            if (!shadowBox(box.baseColor.xyz, box.material.xyz, localOrigin, inverseDirection, parallel, maxDistance))
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
                                                              uint(instance.uv.x), shadowOnly)) return true;
            node = uint(box.baseColor.w);
        }
    }
    return false;
}
float shadowSamplePhase()
{
    // The shadow diagnostic uses positive light indices. A negative value supplies a shadow-only frame index.
    return max(0, max(irradiance[6].w, -irradiance[5].w) - 1);
}
float sceneVisibility(float3 position, float3 normal, float3 light, float softness, float2 blue = -1)
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
    float3 origin = position + normalize(normal) * settings.uv.x;
    if (count == 1 && blue.x < 0)
        return sceneOccluded(header, origin, light, 1e30) ? 0 : 1;
    float  visible = 0;
    [loop] for (uint sampleIndex = 0; sampleIndex < count; sampleIndex++)
    {
        float  radius = count == 1 && blue.x < 0 ? 0 : sqrt((float(sampleIndex) + (blue.x < 0 ? .5 : blue.y)) / count) * softness * .006;
        float  angle  = float(sampleIndex) * 2.39996323 + (blue.x < 0 ? shadowSamplePhase() * 2.39996323 : blue.x);
        float3 ray    = normalize(light + radius * (cos(angle) * tangent + sin(angle) * bitangent));
        visible += sceneOccluded(header, origin, ray, 1e30) ? 0 : 1;
    }
    return visible / count;
}
// Finite shadow segment: local BVH directions are deliberately not normalized,
// so their intersection parameter retains world-space distance under nonuniform scale.
float localLightVisibility(float3 position, float3 normal, float3 lightPosition, float softness, float2 blue = -1)
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
    if (count == 1 && blue.x < 0)
        return sceneOccluded(header, origin, light, max(0, distance - 1e-5)) ? 0 : 1;
    float  visible = 0;
    [loop] for (uint i = 0; i < count; ++i)
    {
        // Deterministic disk around the source, radius in world units.
        float  radius         = count == 1 && blue.x < 0 ? 0 : sqrt((float(i) + (blue.x < 0 ? .5 : blue.y)) / count) * softness * .006;
        float  angle          = float(i) * 2.39996323 + (blue.x < 0 ? shadowSamplePhase() * 2.39996323 : blue.x);
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
    float4 color : TEXCOORD8;
    nointerpolation uint  instanceIndex : TEXCOORD4;
    nointerpolation float winding : TEXCOORD5;
    float2 uv1 : TEXCOORD6;
    nointerpolation float3 dielectric : TEXCOORD7;
};
// Point and line primitives use camera-facing quads in the shared raster pipeline.
float4 expandRasterPrimitive(float4 clip, float4 other, float2 primitive)
{
    uint corner = uint(primitive.x) - 1;
    float aspect = max(.001, abs(cameraRight.w / cameraUp.w));
    const float2 pointCorners[6] = {
        float2(-1, -1), float2(1, -1), float2(-1, 1),
        float2(-1, 1), float2(1, -1), float2(1, 1)
    };
    float2 offset = pointCorners[corner] * .004;
    if (primitive.y > 1.5)
    {
        const bool secondEnd = corner == 1 || corner >= 4;
        float2 direction = other.xy / max(other.w, .001) - clip.xy / max(clip.w, .001);
        direction *= (secondEnd ? -1 : 1) * float2(aspect, 1);
        float2 side = float2(-direction.y, direction.x) * rsqrt(max(dot(direction, direction), 1e-12));
        offset = side * (corner == 0 || corner == 1 || corner == 4 ? -.002 : .002);
    }
    clip.xy += offset * float2(1 / aspect, 1) * clip.w;
    return clip;
}
uint rasterOtherEndpoint(uint id, float2 primitive)
{
    uint corner = uint(primitive.x) - 1;
    return id - corner + (corner == 1 || corner >= 4 ? 0 : 1);
}
VertexOut VSMain(uint id : SV_VertexID, uint instanceId : SV_InstanceID)
{
#ifdef NOVA_D3D12
    id += drawOffsets.x;
    instanceId += drawOffsets.y;
#endif
    bool   scene         = lightDirection.w > .5 && lightDirection.w < 1.5;
    float  palette       = scene ? instances[instanceId].normalMatrix._m03 : 1;
    Vertex v             = skinVertex(id, uint(max(0, palette - 1)), palette > .5);
    uint   materialIndex = uint(max(1, v.position.w)) - 1;
    v.position.w         = 1;
    if (lightDirection.w > .5 && lightDirection.w < 1.5)
    {
        InstanceData instance = instances[instanceId];
        materialIndex += uint(instance.uv.x);
        v.position   = mul(instance.world, v.position);
        v.normal.xyz = mul((float3x3)instance.normalMatrix, v.normal.xyz);
    }
    VertexOut o;
    o.winding = 1;
    if (lightDirection.w > .5 && lightDirection.w < 1.5)
        o.winding = determinant((float3x3)instances[instanceId].world) < 0 ? -1 : 1;
    o.instanceIndex   = materialIndex;
    o.position        = mul(viewProjection, v.position);
    if (v.uv1.z > .5)
    {
        float4 other = v.position;
        if (v.uv1.w > 1.5)
        {
            other = float4(skinVertex(rasterOtherEndpoint(id, v.uv1.zw), uint(max(0, palette - 1)),
                                     palette > .5).position.xyz, 1);
            if (scene)
                other = mul(instances[instanceId].world, other);
        }
        o.position = expandRasterPrimitive(o.position, mul(viewProjection, other), v.uv1.zw);
        o.winding = 0;
    }
    o.worldPosition   = v.position.xyz;
    o.normal          = v.normal.xyz;
    o.uv              = v.uv.xy;
    o.uv1             = v.uv1.xy;
    o.color           = v.color;
    o.generatedNormal = v.uv.z;
    o.dielectric      = float3(v.normal.w, v.uv.w, v.uv1.z > .5 ? 1 : v.uv1.w);
    return o;
}
float4 materialSpecular(InstanceData material)
{
    return float4(material.world._m00, material.world._m10, material.world._m20, material.world._m30);
}
float2 independentTextureUV(InstanceData material, float2 uv, uint map, bool delta = false)
{
    if (!(uint(material.normalMatrix[3][2]) & map))
        return uv;
    float3 mapped = float3(uv, delta ? 0 : 1);
    return float2(
      dot(float3(material.normalMatrix[1][1], material.normalMatrix[2][1], material.normalMatrix[3][1]), mapped),
      dot(float3(material.normalMatrix[0][2], material.normalMatrix[1][2], material.normalMatrix[2][2]), mapped));
}
float4 materialSheen(InstanceData material)
{
#if defined(NOVA_DISABLE_SHEEN)
    return 0;
#else
    return float4(material.normalMatrix[0][0], material.normalMatrix[1][0],
                  material.normalMatrix[2][0], material.normalMatrix[3][0]);
#endif
}
uint materialSheenMaps(InstanceData material)
{
    return uint(material.normalMatrix[0][1]) & 15u;
}
float4 sampleSheenMaps(InstanceData material, float2 uv0, float2 uv1)
{
    uint maps = materialSheenMaps(material);
    float4 result = 1;
    if (maps & 1u)
        result.rgb = coatStrengthTexture.SampleLevel(textureSampler, maps & 4u ? uv1 : uv0, 0).gba;
    if (maps & 2u)
        result.a = coatRoughnessTexture.SampleLevel(textureSampler, maps & 8u ? uv1 : uv0, 0).a;
    return result;
}
float4 materialFilm(InstanceData material, float2 map)
{
    float factor = material.normalMatrix[0][3];
    if (factor <= 0)
        return 0;
    float substrateIor = 1 + ((uint(material.normalMatrix[0][1]) >> 8) & 4095u) / 2047.5;
    return float4(factor * map.x, material.normalMatrix[1][3],
                  lerp(material.normalMatrix[2][3], material.normalMatrix[3][3], map.y), substrateIor);
}
float2 sampleFilmMaps(InstanceData material, float2 uv0, float2 uv1)
{
    uint maps = (uint(material.normalMatrix[0][1]) >> 4) & 15u;
    float2 result = 1;
    if (maps & 1u)
        result.x = coatRoughnessTexture.SampleLevel(textureSampler, maps & 4u ? uv1 : uv0, 0).r;
    if (maps & 2u)
        result.y = coatRoughnessTexture.SampleLevel(textureSampler, maps & 8u ? uv1 : uv0, 0).b;
    return result;
}
float2 materialAnisotropy(InstanceData material, float2 uv0, float2 uv1)
{
    uint packed = uint(material.normalMatrix[3][2]);
    float strength = float((packed >> 2) & 2047u) / 2047;
    float turn = float(packed >> 13) / 2048;
    uint textureFlags = uint(material.normalMatrix[0][1]);
    if (strength > 0 && (textureFlags & (1u << 20)))
    {
        float2 uv = textureFlags & (1u << 21) ? uv1 : uv0;
        uint width, height;
        emissiveTexture.GetDimensions(width, height);
        uint2 pixel = min(uint2(uv * float2(width, height)), uint2(width - 1, height - 1));
        // An axis is periodic; point fetch prevents interpolating across the wrap discontinuity.
        turn += emissiveTexture.Load(int3(pixel, 0)).a * (255.0 / 256);
        strength *= coatNormalTexture.SampleLevel(textureSampler, uv, 0).a;
    }
    return float2(strength, turn);
}
float4 anisotropicFrame(float2 params, float3 normal, float3 tangent, float3 bitangent)
{
    if (params.x <= 0)
        return 0;
    float3 t = anisotropicTangent(normal, tangent);
    float3 b = cross(normal, t);
    if (dot(b, bitangent) < 0)
        b = -b;
    float angle = 3.14159265 * params.y;
    return float4(t * cos(angle) + b * sin(angle), params.x);
}
float4 decodeAnisotropy(float3 n, float2 encoded)
{
    if (encoded.y <= 0)
        return 0;
    float3 t = anisotropicBasis(n);
    float angle = 3.14159265 * encoded.x;
    return float4(t * cos(angle) + cross(n, t) * sin(angle), encoded.y);
}
float2 encodeAnisotropy(float3 n, float4 anisotropy)
{
    if (anisotropy.w <= 0)
        return 0;
    float3 t = anisotropicBasis(n);
    return float2(frac(atan2(dot(anisotropy.xyz, cross(n, t)), dot(anisotropy.xyz, t)) / 3.14159265), anisotropy.w);
}
float2 materialClearcoat(InstanceData material)
{
    return float2(material.world._m01, material.world._m11);
}
float3 dielectricF0(float4 specular)
{
    return min(0.04 * specular.rgb, 1.0) * specular.a;
}
float3 fresnel(float cosine, float3 f0, float f90)
{
    return lerp(f0, f90, pow(1 - saturate(cosine), 5));
}
float3 diffuseFresnel(float cosine, float3 reflection, float4 specular)
{
    // Preserve the core metallic-roughness shading for default materials.
    if (all(specular == 1))
        return reflection;
    float3 dielectric = fresnel(cosine, dielectricF0(specular), specular.a);
    return max(max(dielectric.r, dielectric.g), dielectric.b).xxx;
}
float3 filmDielectricReflectance(float cosine, float4 specular, float4 film)
{
    float3 baseF0 = min(filmBaseReflectance(film.w) * specular.rgb, 1);
    return blendThinFilm(fresnel(cosine, baseF0, 1), cosine, baseF0, film.xyz) * specular.a;
}
float3 materialReflectance(float cosine, float3 base, float metallic, float4 specular, float4 film)
{
    if (film.x <= 0 || film.z <= 0)
        return fresnel(cosine, lerp(dielectricF0(specular), base, metallic), lerp(specular.a, 1, metallic));
    if (metallic <= 0)
        return filmDielectricReflectance(cosine, specular, film);
    float3 conductor = blendThinFilm(fresnel(cosine, base, 1), cosine, base, film.xyz);
    if (metallic >= 1)
        return conductor;
    return lerp(filmDielectricReflectance(cosine, specular, film), conductor, metallic);
}
float3 directLight(
  float3 n, float3 v, float3 l, float3 base, float metallic, float roughness, float4 specular, float3 radiance, float4 film = 0, float4 anisotropy = 0, float4 sheen = 0)
{
    float3 halfVector = v + l;
    float3 h          = halfVector * rsqrt(max(dot(halfVector, halfVector), 1e-8));
    float  nv = max(dot(n, v), 0.001), nl = saturate(dot(n, l)), nh = saturate(dot(n, h));
    float  a = roughness * roughness, a2 = a * a;
    float  d = a2 / (3.14159265 * pow(nh * nh * (a2 - 1) + 1, 2) + 0.0001);
    float  k = pow(roughness + 1, 2) / 8;
    float  g = (nv / (nv * (1 - k) + k)) * (nl / (nl * (1 - k) + k));
    float  vh = dot(v, h);
    float3 f = materialReflectance(vh, base, metallic, specular, film);
    float3 dielectric = film.x > 0 && film.z > 0
      ? f : diffuseFresnel(vh, f, specular);
    float3 diffuse = (1 - dielectric) * (1 - metallic) * base / 3.14159265;
    float specularWeight = d * g / (4 * nv * max(nl, 0.001));
    if (anisotropy.w > 0 && nl > 0)
    {
        float2 alpha = anisotropicRoughness(roughness, anisotropy.w);
        specularWeight = anisotropicD(h, n, anisotropy.xyz, alpha) *
                         anisotropicVisibility(v, l, n, anisotropy.xyz, alpha);
    }
    return sheenLayer(sheen, n, v, l, diffuse + specularWeight * f) * radiance * nl;
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
// Only the swapchain pipeline enables this. Offscreen passes stay linear.
#if defined(NOVA_VULKAN_PRESENT)
[[vk::constant_id(0)]] const uint encodeVulkanOutput = 0;
#endif
float4 presentColor(float4 color)
{
#if defined(NOVA_VULKAN_PRESENT)
    if (encodeVulkanOutput)
        color.rgb = encodeDisplayRGB(color.rgb);
#endif
    return color;
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
    output.position = float4(p, 1, 1);
    output.uv       = float2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
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
        return presentColor(float4(skyParams.z > 1.5 ? diagnosticLinear(color) : color, 1));
    }
    float3 direction = environmentDirection(normalize(input.direction));
    float3 color     = backgroundTexture.Sample(textureSampler, direction).rgb * skyParams.x;
    return presentColor(float4(displayRadiance(color), 1));
}
struct SurfaceData
{
    float3 position, normal, geometricNormal, base, emissive;
    float  metallic, roughness, ao;
    float  opacity, alphaInfo;
    float2 dielectric;
    float4 volume;
    float4 specular;
    float4 specularMap;
    float4 sheen;
    float4 sheenMap;
    float4 film;
    float2 filmMap;
    float4 anisotropy;
    float2 clearcoat;
    float3 coatNormal;
    float4 visibility0, visibility1, visibility2, visibility3;
    uint   useVisibility;
};
struct CoatUVs
{
    float2 strength, roughness, normal;
    uint mask;
    float scale;
};
CoatUVs clearcoatUVs(InstanceData material, float2 uv0, float2 uv1)
{
    CoatUVs p;
    uint sets = uint(material.world._m02);
    p.strength = (sets & 1u) ? uv1 : uv0;
    p.roughness = (sets & 2u) ? uv1 : uv0;
    p.normal = (sets & 4u) ? uv1 : uv0;
    p.mask = uint(material.world._m31);
    p.scale = material.world._m12;
    return p;
}
float3 applyCoatNormal(float3 n, float3 map, float scale, float3 dp1, float3 dp2, float2 duv1, float2 duv2)
{
    float3 t = cross(dp2, n) * duv1.x + cross(n, dp1) * duv2.x;
    float3 b = cross(dp2, n) * duv1.y + cross(n, dp1) * duv2.y;
    float basis = max(dot(t, t), dot(b, b));
    float3 perturbed = (t * map.x + b * map.y) * (scale * rsqrt(max(basis, 1e-20))) + n * map.z;
    return (basis > 1e-20 && dot(perturbed, perturbed) > 1e-12) ? normalize(perturbed) : n;
}
SurfaceData sampleSurface(VertexOut input)
{
    float  alphaInfo  = .125;
    float4 specular   = 1, specularMap = 1;
    float4 sheen = 0, sheenMap = 1;
    float2 filmMap = 1, anisotropy = 0;
    float2 clearcoat  = 0;
    InstanceData materialRecord = (InstanceData)0;
    float2 uv         = input.uv;
    float2 aoUV       = uv;
    float2 baseUV     = uv;
    float4 baseFactor = baseColorFactor, matFactor = materialFactor, emissionFactor = emissiveFactor;
    if (lightDirection.w > .5)
    {
        InstanceData instance = instances[input.instanceIndex];
        baseUV                = wrapBaseUV(uv, instance.emissive.w) * instance.uv.xy + instance.uv.zw;
        uv                    = frac(uv) * instance.uv.xy + instance.uv.zw;
        aoUV                  = instance.world._m21 > .5 ? frac(input.uv1) * instance.uv.xy + instance.uv.zw : uv;
        baseFactor            = instance.baseColor;
        matFactor             = instance.material;
        emissionFactor.rgb    = instance.emissive.rgb;
        alphaInfo             = instance.emissive.w;
        specular              = materialSpecular(instance);
        uint specularMaps = uint(instance.world._m22);
        float2 uv1 = frac(input.uv1) * instance.uv.xy + instance.uv.zw;
        if (specularMaps & 1u)
            specularMap.a = normalTexture.SampleLevel(textureSampler, specularMaps & 4u ? uv1 : uv, 0).a;
        if (specularMaps & 2u)
            specularMap.rgb = occlusionTexture.SampleLevel(textureSampler, specularMaps & 8u ? uv1 : uv, 0).gba;
        anisotropy = materialAnisotropy(instance, uv, uv1);
        if (instance.normalMatrix[0][3] > 0)
            filmMap = sampleFilmMaps(instance, uv, uv1);
        sheen = materialSheen(instance);
        if (any(sheen.rgb > 0))
            sheenMap = sampleSheenMaps(instance, uv, uv1);
        clearcoat             = materialClearcoat(instance);
        materialRecord        = instance;
    }
    float4 baseSample = baseTexture.Sample(textureSampler, baseUV) * input.color;
    float3 base       = baseSample.rgb * baseFactor.rgb;
    float3 n          = normalize(input.normal);
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
    float3 coatNormal = n;
    CoatUVs coatUV = clearcoatUVs(materialRecord, input.uv, input.uv1);
    float2 coatDX = ddx(coatUV.normal), coatDY = ddy(coatUV.normal);
    float2 strengthDX = ddx(coatUV.strength), strengthDY = ddy(coatUV.strength);
    float2 roughDX = ddx(coatUV.roughness), roughDY = ddy(coatUV.roughness);
    if (clearcoat.x > 0)
    {
        float2 atlasNormalUV = frac(coatUV.normal) * materialRecord.uv.xy + materialRecord.uv.zw;
        if (coatUV.mask & 1u)
            clearcoat.x *= coatStrengthTexture.SampleGrad(textureSampler,
                frac(coatUV.strength) * materialRecord.uv.xy + materialRecord.uv.zw,
                strengthDX * materialRecord.uv.xy, strengthDY * materialRecord.uv.xy).r;
        if (coatUV.mask & 2u)
            clearcoat.y *= coatRoughnessTexture.SampleGrad(textureSampler,
                frac(coatUV.roughness) * materialRecord.uv.xy + materialRecord.uv.zw,
                roughDX * materialRecord.uv.xy, roughDY * materialRecord.uv.xy).g;
        if (coatUV.mask & 4u)
            coatNormal = applyCoatNormal(n, coatNormalTexture.SampleGrad(textureSampler, atlasNormalUV,
                coatDX * materialRecord.uv.xy, coatDY * materialRecord.uv.xy).xyz * 2 - 1,
                coatUV.scale, dp1, dp2, coatDX, coatDY);
    }
    float2 normalUV = independentTextureUV(materialRecord, input.uv, 1u);
    float2 normalAtlasUV = lightDirection.w > .5 ? frac(normalUV) * materialRecord.uv.xy + materialRecord.uv.zw : normalUV;
    float2 duv1 = ddx(normalUV), duv2 = ddy(normalUV);
    float3 p2perp = cross(dp2, n), p1perp = cross(n, dp1);
    float3 t = p2perp * duv1.x + p1perp * duv2.x, b = p2perp * duv1.y + p1perp * duv2.y;
    float  scale   = rsqrt(max(max(dot(t, t), dot(b, b)), 0.00001));
    float3 sampled = normalTexture.Sample(textureSampler, normalAtlasUV).xyz * 2 - 1;
    sampled.xy *= matFactor.z;
    n                    = normalize(t * scale * sampled.x + b * scale * sampled.y + n * sampled.z);
    bool independentMR = (uint(materialRecord.normalMatrix[3][2]) & 2u) != 0;
    float2 mrUV = independentMR ? frac(independentTextureUV(materialRecord, input.uv, 2u)) *
      materialRecord.uv.xy + materialRecord.uv.zw : uv;
    float4 mr = materialTexture.Sample(textureSampler, mrUV);
    bool useMirrorRoughness = irradiance[1].w > .5 && irradiance[3].w > .5
                               && instances[uint(irradiance[3].w) - 1].world._m13 > .5;
    float metallic = saturate(mr.b * matFactor.x);
    float roughness = clamp(mr.g * matFactor.y, useMirrorRoughness ? .02 : .06, 1);
    float       ao = lerp(1, occlusionTexture.Sample(textureSampler, aoUV).r, matFactor.w);
    SurfaceData result;
    result.useVisibility = 0;
    result.specular = specular * specularMap;
    result.specularMap = specularMap;
    result.sheen = sheen * sheenMap;
    result.sheenMap = sheenMap;
    result.film = materialFilm(materialRecord, filmMap);
    result.filmMap = filmMap;
    result.clearcoat = clearcoat;
    result.coatNormal = coatNormal;
    result.visibility0 = result.visibility1 = result.visibility2 = result.visibility3 = 1;
    result.opacity                                                                    = baseSample.a * baseFactor.a;
    result.dielectric = emissiveFactor.w > .5 ? input.dielectric.xy : 0;
    uint materialFlags = uint(alphaInfo);
    result.volume = float4(materialRecord.world._m03, materialRecord.world._m13,
                          materialRecord.world._m23, materialRecord.world._m33 * input.dielectric.z);
    if (independentMR && (materialFlags & 1536u))
        mr.ra = materialTexture.SampleLevel(textureSampler, uv, 0).ra;
    if ((materialFlags & 512u) != 0)
        result.dielectric.x *= mr.a;
    if ((materialFlags & 1024u) != 0)
        result.volume.w *= mr.r;
    result.alphaInfo                                                                  = alphaInfo;
    result.position                                                                   = input.worldPosition;
    result.normal                                                                     = n;
    result.anisotropy = anisotropicFrame(anisotropy, n, t, b);
    result.geometricNormal                                                            = normalize(input.normal);
    result.base                                                                       = base;
    result.metallic                                                                   = metallic;
    result.roughness                                                                  = roughness;
    result.ao                                                                         = ao;
    float2 emissionUV = (uint(alphaInfo) & 16u) ? frac(input.uv1) * materialRecord.uv.xy + materialRecord.uv.zw : uv;
    result.emissive = emissiveTexture.Sample(textureSampler, emissionUV).rgb * emissionFactor.rgb;
    return result;
}
float hybridVisibility(SurfaceData s, uint index)
{
    return index < 4    ? s.visibility0[index]
           : index < 8  ? s.visibility1[index - 4]
           : index < 12 ? s.visibility2[index - 8]
                        : s.visibility3[min(index - 12, 3)];
}
float clearcoatTransmission(SurfaceData s, float3 v)
{
    return 1 - s.clearcoat.x * fresnel(dot(s.coatNormal, v), 0.04, 1).x;
}
float3 surfaceDirectLight(SurfaceData s, float3 v, float3 l, float3 radiance, float transmission)
{
    float3 color = directLight(s.normal, v, l, s.base, s.metallic, s.roughness, s.specular, radiance, s.film, s.anisotropy, s.sheen);
    if (s.clearcoat.x > 0)
        return color * transmission + s.clearcoat.x * directLight(
          s.coatNormal, v, l, 0.04, 1, max(.06, s.clearcoat.y), 1, radiance);
    return color;
}
float3 clearcoatEnvironment(SurfaceData s, float3 v)
{
    if (s.clearcoat.x <= 0)
        return 0;
    float3 n = s.coatNormal;
    float3 r = reflect(-v, n);
    float roughness = max(.06, s.clearcoat.y);
    float nv = saturate(dot(n, v));
    if (cameraForward.w > .5)
    {
        float3 filtered = environmentTexture.SampleLevel(
          textureSampler, environmentDirection(r), roughness * environmentParams.w).rgb;
        float2 brdf = environmentBRDF(roughness, nv);
        return s.clearcoat.x * filtered * (0.04 * brdf.x + brdf.y) * environmentParams.z;
    }
    return s.clearcoat.x * studio(r, roughness) * fresnel(nv, 0.04, 1);
}
// Rectangular emitters store their half extents in world._m30/_m31 and
// their two planar axes in the first two world columns.
float3 rectangleRight(InstanceData light)
{
    return float3(light.world._m00, light.world._m10, light.world._m20);
}
float3 rectangleUp(InstanceData light)
{
    return float3(light.world._m01, light.world._m11, light.world._m21);
}
float3 rectanglePosition(InstanceData light, float2 offset)
{
    return light.material.xyz + rectangleRight(light) * offset.x + rectangleUp(light) * offset.y;
}
float3 rectangleSample(InstanceData light, uint index, uint count)
{
    if (count == 1)
        return light.material.xyz;
    // A stratified rectangle sequence. Temporal Hybrid rotates the sequence
    // over the emitter so penumbras converge at low per-frame sample counts.
    float radical = .5 * float(index & 1) + .25 * float((index >> 1) & 1)
                    + .125 * float((index >> 2) & 1) + .0625 * float((index >> 3) & 1);
    float2 sequence = float2((float(index) + .5) / count, radical + .5 / count);
    float frame = max(0, irradiance[6].w - 1);
    float2 cell = frac(sequence + frame * float2(.61803399, .38196601)) * 2 - 1;
    return rectanglePosition(light, cell * float2(light.world._m30, light.world._m31));
}
uint rectangleShadowSamples()
{
    return clamp(uint(irradiance[1].w > .5 ? irradiance[2].w
                : instances[uint(irradiance[3].w) - 1].emissive.w), 1u, 16u);
}
float rectangleVisibility(InstanceData light, float3 position, float3 normal)
{
    float visibility = 0;
    uint samples = rectangleShadowSamples();
    [loop] for (uint i = 0; i < samples; ++i)
        visibility += localLightVisibility(position, normal, rectangleSample(light, i, samples), 0);
    return visibility / samples;
}
float rectangleRange(float distance, float range)
{
    return range > 0 ? pow(saturate(1 - pow(distance / range, 4)), 2) : 1;
}
float3 rectangleSpecular(InstanceData light, SurfaceData s, float3 v, float3 normal, float roughness,
                         float3 f0, float f90, float planeDistance, bool castShadow)
{
    float nv = dot(normal, v);
    if (nv <= 0)
        return 0;
    float3 ray = reflect(-v, normal);
    float facing = -dot(ray, light.emissive.xyz);
    if (facing <= .001 || dot(ray, s.geometricNormal) <= 0)
        return 0;
    float distance = planeDistance / facing;
    float3 relative = s.position + distance * ray - light.material.xyz;
    float2 coord = float2(dot(relative, rectangleRight(light)), dot(relative, rectangleUp(light)));
    float2 halfExtent = float2(light.world._m30, light.world._m31);
    // Broaden the projected rectangle by the width of the glossy lobe. Normalize
    // the broadened footprint so narrow softboxes lose intensity as they blur.
    float spread = max(.001, roughness * roughness * distance * .65);
    float2 edge = smoothstep(-spread, spread, halfExtent - abs(coord));
    float2 energy = halfExtent / (halfExtent + spread);
    float response = edge.x * edge.y * energy.x * energy.y * rectangleRange(distance, light.material.w);
    if (response <= 0)
        return 0;
    if (castShadow)
        response *= localLightVisibility(s.position, s.geometricNormal,
            rectanglePosition(light, clamp(coord, -halfExtent, halfExtent)), 0);
    float2 brdf = environmentBRDF(roughness, saturate(nv));
    return light.baseColor.rgb * light.baseColor.w * response * (f0 * brdf.x + f90 * brdf.y);
}
float3 rectangleGGX(float3 n, float3 v, float3 l, float3 h, float roughness, float3 f0, float f90)
{
    float nv = saturate(dot(n, v)), nl = saturate(dot(n, l)), nh = saturate(dot(n, h));
    if (nv <= 0 || nl <= 0)
        return 0;
    float a2 = roughness * roughness * roughness * roughness;
    float d = nh * nh * (a2 - 1) + 1;
    float k = (roughness + 1) * (roughness + 1) / 8;
    float visibility = .25 / ((nv * (1 - k) + k) * (nl * (1 - k) + k));
    return fresnel(saturate(dot(v, h)), f0, f90) * (a2 * visibility / (3.14159265 * d * d));
}
float3 rectangleDirectLight(InstanceData light, SurfaceData s, float3 v, float coatTransmission,
                           bool secondaryLighting)
{
    float planeDistance = dot(s.position - light.material.xyz, light.emissive.xyz);
    if (planeDistance <= 0 || light.baseColor.w <= 0)
        return 0;

    float3 f0 = lerp(dielectricF0(s.specular), s.base, s.metallic);
    float f90 = lerp(s.specular.a, 1, s.metallic);
    float3 diffuse = 0, sheen = 0, roughSpecular = 0, roughCoat = 0;
    bool castShadow = light.uv.z > .5;
    uint samples = secondaryLighting ? 1 : castShadow ? rectangleShadowSamples() : 8;
    float baseBlend = secondaryLighting || samples == 1 ? 0 : smoothstep(.35, .65, s.roughness);
    float coatBlend = secondaryLighting || samples == 1 || s.clearcoat.x <= 0
      ? 0 : smoothstep(.35, .65, s.clearcoat.y);
    float3 emitted = light.baseColor.rgb * light.baseColor.w * (4 * light.world._m30 * light.world._m31 / samples);
    if (s.metallic < 1 || any(s.sheen.rgb > 0) || baseBlend > 0 || coatBlend > 0)
    {
        [loop] for (uint i = 0; i < samples; ++i)
        {
            float3 samplePosition = rectangleSample(light, i, samples);
            float3 delta = samplePosition - s.position;
            float distanceSquared = dot(delta, delta);
            float distance = sqrt(distanceSquared);
            float3 l = delta / max(distance, 1e-5);
            float weight = saturate(dot(s.normal, l)) * saturate(-dot(light.emissive.xyz, l))
                           * rectangleRange(distance, light.material.w) / max(distanceSquared, .0001);
            float coatWeight = coatBlend > 0 ? saturate(dot(s.coatNormal, l))
                * saturate(-dot(light.emissive.xyz, l)) * rectangleRange(distance, light.material.w)
                / max(distanceSquared, .0001) : 0;
            if (weight <= 0 && coatWeight <= 0)
                continue;
            if (castShadow)
            {
                float visible = localLightVisibility(s.position, s.geometricNormal, samplePosition, 0);
                weight *= visible;
                coatWeight *= visible;
            }
            float3 h = (v + l) * rsqrt(max(dot(v + l, v + l), 1e-8));
            diffuse += weight * (1 - diffuseFresnel(dot(v, h), fresnel(dot(v, h), f0, f90), s.specular));
            sheen += weight * sheenLayer(s.sheen, s.normal, v, l, 0);
            if (baseBlend > 0)
                roughSpecular += weight * rectangleGGX(s.normal, v, l, h, s.roughness, f0, f90);
            if (coatBlend > 0)
                roughCoat += coatWeight * rectangleGGX(s.coatNormal, v, l, h, s.clearcoat.y, .04.xxx, 1);
        }
        sheen *= emitted;
        diffuse *= emitted * (1 - s.metallic) * s.base / 3.14159265;
    }
    float3 specular = roughSpecular * emitted * baseBlend;
    if (baseBlend < 1)
        specular += (1 - baseBlend) * rectangleSpecular(light, s, v, s.normal, s.roughness, f0, f90,
                                                       planeDistance, castShadow && !secondaryLighting);
    float3 coat = 0;
    if (s.clearcoat.x > 0)
    {
        coat = roughCoat * emitted * coatBlend;
        if (coatBlend < 1)
            coat += (1 - coatBlend) * rectangleSpecular(light, s, v, s.coatNormal, max(.02, s.clearcoat.y),
                                                       .04.xxx, 1, planeDistance, castShadow && !secondaryLighting);
        coat *= s.clearcoat.x;
    }
    float sheenScale = 1 - max(s.sheen.r, max(s.sheen.g, s.sheen.b)) * sheenAlbedo(saturate(dot(s.normal, v)), s.sheen.a);
    return ((diffuse + specular) * sheenScale + sheen) * coatTransmission + coat;
}

float3 surfaceRadiance(SurfaceData s, float3 raySpecular, float reflectionMix, float3 viewDirection,
                       bool tracedClearcoat = false, bool secondaryLighting = false, float probeSkyCoverage = 0)
{
    float3 base = s.base;
    if (emissiveFactor.w < .5)
        return base;
    float3 n = s.normal, v = viewDirection;
    if (!secondaryLighting)
        s.roughness = max(.06, s.roughness);
    float  metallic = s.metallic, roughness = s.roughness, ao = s.ao;
    float  coatTransmission = clearcoatTransmission(s, v);
    bool   collection = false;
    uint   header     = 0;
    if (irradiance[3].w > .5)
    {
        header     = uint(irradiance[3].w) - 1;
        collection = instances[header].uv.w > .5;
    }
    float3 color = 0;
    float3 coatEnvironment = clearcoatEnvironment(s, v);
    float3 baseEnvironment, diffuseAmbient;
    if (collection)
    {
        uint count = uint(instances[header].uv.y), offset = uint(instances[header].uv.z);
        [loop] for (uint i = 0; i < count; ++i)
        {
            InstanceData source      = instances[offset + i];
            uint         type        = uint(source.emissive.w);
            if (type == 3)
            {
                color += rectangleDirectLight(source, s, v, coatTransmission, secondaryLighting);
                continue;
            }
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
            color += surfaceDirectLight(
              s, v, l, source.baseColor.rgb * source.baseColor.w * attenuation * visibility, coatTransmission);
        }
    }
    else
        color = surfaceDirectLight(
          s,
          v,
          lightDirection.xyz,
          lightColorIntensity.rgb * lightColorIntensity.w
            * (s.useVisibility?s.visibility0.x:sceneVisibility(
              s.position,
              s.geometricNormal,
              lightDirection.xyz,
              irradiance[3].w > .5 ? instances[uint(irradiance[3].w) - 1].emissive.z : 0)),
          coatTransmission);
    float3 f0 = lerp(dielectricF0(s.specular), base, metallic);
    float  f90 = lerp(s.specular.a, 1, metallic);
    float  nv = saturate(dot(n, v));
    float3 f = materialReflectance(nv, base, metallic, s.specular, s.film);
    float3 diffuse = (1 - metallic) * base;
    float3 anisotropicReflection = anisotropicEnvironmentDirection(n, v, s.anisotropy.xyz, roughness, s.anisotropy.w);
    bool filmed = s.film.x > 0 && s.film.z > 0;
    float3 diffuseEnergy = 1 - (filmed ? filmDielectricReflectance(nv, s.specular, s.film) : diffuseFresnel(nv, f, s.specular));
    if (filmed)
    {
        float grazing = pow(1 - nv, 5);
        f0 = saturate((f - f90 * grazing) / max(1 - grazing, 1e-4));
    }
    if (cameraForward.w > 0.5)
    {
        float3 reflected = environmentDirection(anisotropicReflection);
        float3 prefiltered =
          environmentTexture.SampleLevel(textureSampler, reflected, roughness * environmentParams.w).rgb;
        float2 brdf    = environmentBRDF(roughness, saturate(dot(n, v)));
        diffuseAmbient = diffuseEnergy * diffuse * diffuseEnvironment(n) * environmentParams.z;
        baseEnvironment = prefiltered * (f0 * brdf.x + f90 * brdf.y) * environmentParams.z;
    }
    else
    {
        if (!collection)
            color += surfaceDirectLight(s, v, normalize(float3(1, 0.7, -1)), float3(1, 1.6, 2.7), coatTransmission);
        diffuseAmbient = diffuse * (all(s.specular == 1) && !filmed ? float3(1, 1, 1) : diffuseEnergy) * studio(n, 1) * 0.4;
        baseEnvironment = studio(anisotropicReflection, roughness) * f;
    }
    if (any(s.sheen.rgb > 0))
    {
        float energy = sheenAlbedo(nv, s.sheen.a);
        float baseWeight = 1 - energy * max(s.sheen.r, max(s.sheen.g, s.sheen.b));
        float3 reflected = reflect(-v, n);
        float3 incident = cameraForward.w > .5
          ? environmentTexture.SampleLevel(textureSampler, environmentDirection(reflected),
              lerp(.5, 1, s.sheen.a) * environmentParams.w).rgb * environmentParams.z
          : studio(reflected, lerp(.5, 1, s.sheen.a));
        diffuseAmbient *= baseWeight;
        baseEnvironment = baseEnvironment * baseWeight + s.sheen.rgb * (energy * incident);
    }
    diffuseAmbient *= 1 - probeSkyCoverage;
    if (tracedClearcoat)
        color += (diffuseAmbient * coatTransmission
                  + lerp(baseEnvironment * coatTransmission + coatEnvironment, raySpecular, reflectionMix)) * ao;
    else
        color += ((diffuseAmbient + lerp(baseEnvironment, raySpecular, reflectionMix)) * coatTransmission
                  + coatEnvironment) * ao;
    color += s.emissive * coatTransmission;
    return color;
}
float4 shadeSurface(SurfaceData s)
{
    float3 base = s.base;
    if (emissiveFactor.w < .5 || (uint(s.alphaInfo) & 8u) != 0)
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
    bool front = winding == 0 || (winding < 0 ? !face : face);
    uint flags = (uint)s.alphaInfo, mode = flags & 3;
    if ((flags & 4) == 0 && !front)
        discard;
    if (mode == 1 && s.opacity < frac(s.alphaInfo) * 4)
        discard;
    bool blended = mode == 2 || s.dielectric.x > 0;
    if (transparent ? (!blended || (mode == 2 && s.opacity <= 0)) : blended)
        discard;
    if (!front)
    {
        s.normal          = -s.normal;
        s.geometricNormal = -s.geometricNormal;
        s.coatNormal = -s.coatNormal;
    }
}
float4 PSMain(VertexOut input, bool face : SV_IsFrontFace)
  : SV_Target
{
    SurfaceData s = sampleSurface(input);
    materialCoverage(s, face, input.winding, false);
    return presentColor(shadeSurface(s));
}
[[vk::binding(17, 1)]] Texture2D<float4> gBase : register(t17);
float3 glassRadiance(SurfaceData s, float2 pixel)
{
    uint width, height;
    gBase.GetDimensions(width, height);
    float2 extent = float2(width, height);
    float2 uv = pixel / extent;
    // Thin glass uses the opaque scene at the projected surface. Roughness
    // broadens that sample without requiring an extra full-resolution pass.
    float3 transmitted = gBase.SampleLevel(textureSampler, uv, 0).rgb;
    float transmissionRoughness = s.roughness * saturate(2 * (s.dielectric.y - 1));
    if (transmissionRoughness > .1)
    {
        float radius = min(16, transmissionRoughness * transmissionRoughness * .02 * height);
        const float2 taps[4] = {float2(-1, 0), float2(1, 0), float2(0, -1), float2(0, 1)};
        [unroll] for (int i = 0; i < 4; ++i)
            transmitted += gBase.SampleLevel(textureSampler,
                clamp(uv + taps[i] * radius / extent, 1.5 / extent, 1 - 1.5 / extent), 0).rgb;
        transmitted *= .2;
    }

    float3 view = cameraUp.w < 0 ? -cameraForward.xyz : normalize(eyeExposure.xyz - s.position);
    float3 reflectedDirection = reflect(-view, s.normal);
    float3 reflected = cameraForward.w > .5
        ? environmentTexture.SampleLevel(textureSampler, environmentDirection(reflectedDirection),
                                        s.roughness * environmentParams.w).rgb * environmentParams.z
        : studio(reflectedDirection, s.roughness);
    // The environment contains only distant lighting. Local softboxes must also
    // contribute to the reflection of a transmitting surface.
    if (emissiveFactor.w > .5 && irradiance[3].w > .5)
    {
        uint header = uint(irradiance[3].w) - 1;
        uint count = uint(instances[header].uv.y), offset = uint(instances[header].uv.z);
        [loop] for (uint i = 0; i < count; ++i)
        {
            InstanceData light = instances[offset + i];
            if (uint(light.emissive.w) != 3 || light.baseColor.w <= 0)
                continue;
            float planeDistance = dot(s.position - light.material.xyz, light.emissive.xyz);
            if (planeDistance > 0)
                reflected += rectangleSpecular(light, s, view, s.normal, s.roughness, 1.xxx, 1,
                                               planeDistance, light.uv.z > .5);
        }
    }
    float f = (s.dielectric.y - 1) / (s.dielectric.y + 1);
    float3 reflectance = s.dielectric.y <= 1.00001 ? 0 : fresnel(saturate(dot(s.normal, view)), (f * f).xxx, 1);
    reflectance = blendThinFilm(reflectance, saturate(dot(s.normal, view)), (f * f).xxx, s.film.xyz);
    float3 reflection = applyFog(displayRadiance(reflected), s.position) * reflectance;
    float3 attenuation = s.volume.w > 0 ? pow(saturate(s.volume.rgb), s.volume.w) : 1;
    return transmitted * s.base * attenuation * (1 - reflectance) + reflection;
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
    float3 color = shadeSurface(s).rgb;
    if (s.dielectric.x > 0)
    {
        color = lerp(color, glassRadiance(s, input.position.xy), s.dielectric.x * (1 - s.metallic));
        if ((uint(s.alphaInfo) & 3) != 2)
            s.opacity = 1;
    }
    o.accumulation = float4(color * s.opacity * weight, s.opacity * weight);
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
    float4 clearcoat : SV_Target4;
    float4 specularMap : SV_Target5;
    float4 filmMap : SV_Target6;
};
ModelGBuffer GBufferPSMain(VertexOut input, bool face : SV_IsFrontFace)
{
    SurfaceData s = sampleSurface(input);
    materialCoverage(s, face, input.winding, false);
    ModelGBuffer o;
    o.baseRoughness = float4(s.base, s.roughness);
    o.normals       = float4(octEncode(s.normal), octEncode(s.geometricNormal));
    // Split logarithmic view depth avoids half-float world-position quantization.
    // Both halves survive RGBA16F storage, preserving near/far scale without an extra target.
    float depth        = log2(max(1e-20, dot(s.position - eyeExposure.xyz, cameraForward.xyz)));
    float high         = f16tof32(f32tof16(depth));
    // Material slots sit immediately before the globals record. The relative
    // index fits exactly in half precision even when the scene has many instances.
    float slotDistance = lightDirection.w > .5 && irradiance[3].w > .5
                         ? irradiance[3].w - 1 - float(input.instanceIndex) : 0;
    o.positionAO       = float4(high, depth - high, (uint(s.alphaInfo) & 8u) ? -1 : s.ao, slotDistance);
    o.emissiveMetallic = float4(s.emissive, s.metallic);
    o.clearcoat        = float4(octEncode(s.coatNormal), s.clearcoat);
    o.specularMap = s.specularMap;
    o.filmMap = float4(s.filmMap, encodeAnisotropy(s.normal, s.anisotropy));
    if (lightDirection.w > .5)
    {
        InstanceData material = instances[input.instanceIndex];
        if (materialSheenMaps(material) & 3u)
        {
            if (uint(material.world._m22) & 3u)
            {
                // Two 7-bit samples per half component. The exponent offset avoids half subnormals.
                uint4 packed = uint4(saturate(s.specularMap) * 127 + .5) |
                              (uint4(saturate(s.sheenMap) * 127 + .5) << 7);
                o.specularMap = f16tof32(packed + 1024);
            }
            else
                o.specularMap = s.sheenMap;
        }
    }
    return o;
}
[[vk::binding(18, 1)]] Texture2D<float4> gNormals : register(t18);
[[vk::binding(19, 1)]] Texture2D<float4> gPosition : register(t19);
[[vk::binding(20, 1)]] Texture2D<float4> gEmission : register(t20);
[[vk::binding(31, 1)]] Texture2D<float4> gClearcoat : register(t31);
[[vk::binding(33, 1)]] Texture2D<float4> gSpecularMap : register(t33);
[[vk::binding(34, 1)]] Texture2D<float4> gFilmMap : register(t34);
void deferredMaterialLayers(inout SurfaceData surface, InstanceData material, int3 pixel)
{
    float4 modulation = gSpecularMap.Load(pixel);
    float4 specular = 1, sheen = 1;
    bool useSpecularMaps = (uint(material.world._m22) & 3u) != 0;
    bool useSheenMaps = (materialSheenMaps(material) & 3u) != 0;
    if (useSpecularMaps && useSheenMaps)
    {
        uint4 packed = f32tof16(modulation) - 1024;
        specular = float4(packed & 127u) / 127;
        sheen = float4(packed >> 7) / 127;
    }
    else if (useSpecularMaps)
        specular = modulation;
    else if (useSheenMaps)
        sheen = modulation;
    surface.specular = materialSpecular(material) * specular;
    surface.sheen = materialSheen(material) * sheen;
    float4 filmAndAnisotropy = gFilmMap.Load(pixel);
    surface.film = materialFilm(material, filmAndAnisotropy.rg);
    surface.anisotropy = decodeAnisotropy(surface.normal, filmAndAnisotropy.ba);
}
[[vk::binding(21, 1)]] Texture2D<float4> hybridShadow0 : register(t21);
[[vk::binding(22, 1)]] Texture2D<float4> hybridShadow1 : register(t22);
[[vk::binding(23, 1)]] Texture2D<float4> hybridShadow2 : register(t23);
[[vk::binding(24, 1)]] Texture2D<float4> hybridShadow3 : register(t24);
float3 surfacePosition(SkyOut input, float2 encodedDepth)
{
    float depth = exp2(encodedDepth.x + encodedDepth.y);
    if (cameraUp.w >= 0)
        return eyeExposure.xyz + input.direction * depth;
    float2 ndc = float2(input.uv.x * 2 - 1, 1 - input.uv.y * 2);
    return eyeExposure.xyz + cameraForward.xyz * depth + cameraRight.xyz * (ndc.x * cameraRight.w)
           + cameraUp.xyz * (ndc.y * abs(cameraUp.w));
}
bool localLightCastsAt(InstanceData light, float3 position)
{
    float3 delta = light.material.xyz - position;
    float distance = length(delta);
    if (light.material.w > 0 && distance >= light.material.w)
        return false;
    if (uint(light.emissive.w) != 2)
        return true;
    float3 direction = distance > .000001 ? delta / distance : float3(0, 1, 0);
    float cone = dot(-direction, light.emissive.xyz);
    // Smooth cones have zero weight at the boundary. A hard cone includes it.
    return light.uv.x > light.uv.y + .000001 ? cone > light.uv.y : cone >= light.uv.y;
}
// Sixteen independent light visibilities, four light slots per target.
struct HybridShadowTargets
{
    float4 slots0 : SV_Target0;
    float4 slots1 : SV_Target1;
    float4 slots2 : SV_Target2;
    float4 slots3 : SV_Target3;
};
HybridShadowTargets HybridShadowPSMain(SkyOut input)
{
    int3 pixel = int3(int2(input.position.xy), 0);
    if (irradiance[1].z > .5)
    {
        uint width, height;
        gBase.GetDimensions(width, height);
        int2 extent = int2(width, height) - 2;
        int2 full = (pixel.xy - 1) * 2;
        pixel.xy = min(full, extent - 1) + 1;
        input.uv = (float2(pixel.xy) - .5) / float2(extent);
        float2 ndc = input.uv * float2(2, -2) + float2(-1, 1);
        input.direction = cameraForward.xyz + ndc.x * cameraRight.w * cameraRight.xyz
                          + ndc.y * cameraUp.w * cameraUp.xyz;
    }
    HybridShadowTargets output;
    float        visibility[16];
    [unroll] for (uint j = 0; j < 16; j++) visibility[j] = 1;
    if (gBase.Load(pixel).w > 0 && (irradiance[4].w > .5 || irradiance[1].z > .5) && irradiance[3].w > .5)
    {
        float3 position = surfacePosition(input, gPosition.Load(pixel).xy), normal = octDecode(gNormals.Load(pixel).zw);
        uint   header        = uint(irradiance[3].w) - 1;
        InstanceData globals = instances[header];
        float2 blue = -1;
#if defined(NOVA_HYBRID_BLUE_NOISE)
        if (uint(globals.world._m33) & 2u)
        {
            uint frame = uint(shadowSamplePhase());
            uint2 at = uint2(pixel.xy - 1);
            blue = float2(novaBluePhase(at, frame),
                          novaBluePhase(at.yx + uint2(7, 19), frame) / 6.28318531);
        }
#endif
        if (globals.uv.w > .5)
        {
            uint count = min(16, uint(globals.uv.y)), offset = uint(globals.uv.z);
            [loop] for (uint i = 0; i < count; i++)
            {
                InstanceData light = instances[offset + i];
                if (light.uv.z <= .5 || light.baseColor.w <= 0)
                    continue;
                uint type = uint(light.emissive.w);
                if (type == 3)
                {
                    // The lighting pass integrates the emitter's visibility per sample.
                    if (irradiance[5].w == float(i + 1))
                        visibility[i] = rectangleVisibility(light, position, normal);
                }
                else if (type == 0)
                    visibility[i] = sceneVisibility(position, normal, light.emissive.xyz, light.uv.w, blue);
                else
                {
                    // The selected-light diagnostic shows raw visibility outside the light influence too.
                    bool showRawVisibility = irradiance[5].w == float(i + 1);
                    if (showRawVisibility || localLightCastsAt(light, position))
                        visibility[i] = localLightVisibility(position, normal, light.material.xyz, light.uv.w, blue);
                }
            }
        }
        else
            visibility[0] = sceneVisibility(position, normal, lightDirection.xyz, globals.emissive.z, blue);
    }
    output.slots0 = float4(visibility[0], visibility[1], visibility[2], visibility[3]);
    output.slots1 = float4(visibility[4], visibility[5], visibility[6], visibility[7]);
    output.slots2 = float4(visibility[8], visibility[9], visibility[10], visibility[11]);
    output.slots3 = float4(visibility[12], visibility[13], visibility[14], visibility[15]);
    return output;
}
float3 diagnosticLinear(float3 c)
{
    c = saturate(c);
    return float3(
      c.x <= .04045 ? c.x / 12.92 : pow((c.x + .055) / 1.055, 2.4),
      c.y <= .04045 ? c.y / 12.92 : pow((c.y + .055) / 1.055, 2.4),
      c.z <= .04045 ? c.z / 12.92 : pow((c.z + .055) / 1.055, 2.4));
}
// Cosine-weighted finite hemisphere visibility. AO only attenuates indirect lighting.
float hybridAO(float3 position, float3 normal, float rotation)
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
        float  r = sqrt((i + .5) / samples), phi = i * 2.39996323 + rotation;
        float3 direction = tangent * (r * cos(phi)) + bitangent * (r * sin(phi)) + normal * sqrt(1 - r * r);
        visible += sceneOccluded(header, origin, direction, radius, false) ? 0 : 1;
    }
    return pow(saturate(visible / max(1, samples)), strength);
}
// Reuse the reciprocal direction for all nodes of an instance's BVH.
bool nearestHitBox(float3 low, float3 high, float3 origin, float3 reciprocal, bool3 parallel, float distance)
{
    float3 pad = 1e-5 * max(1, max(abs(low), abs(high)));
    float3 lo = low - pad - origin, hi = high + pad - origin;
    if (any(parallel & (lo > 0 | hi < 0)))
        return false;
    float3 a = lo * reciprocal, b = hi * reciprocal;
    float3 nearT = min(a, b), farT = max(a, b);
    nearT = float3(parallel.x ? 0 : nearT.x, parallel.y ? 0 : nearT.y, parallel.z ? 0 : nearT.z);
    farT = float3(parallel.x ? distance : farT.x, parallel.y ? distance : farT.y, parallel.z ? distance : farT.z);
    return min(distance, min(min(farT.x, farT.y), farT.z)) >= max(0, max(max(nearT.x, nearT.y), nearT.z));
}
// Keep world-distance parameters through scaled instances and apply the same material coverage as shadows.
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
        if (instances[i].baseColor.w < 0
            && !shadowBox(instances[i].baseColor.xyz, instances[i].material.xyz, origin, direction, distance))
            continue;
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
        bool3 parallel = abs(d) < 1e-15;
        float3 reciprocal = rcp(float3(parallel.x ? 1 : d.x, parallel.y ? 1 : d.y, parallel.z ? 1 : d.z));
        uint   materialBase = uint(instances[i].uv.x);
        uint   root = header + 1 + uint(instances[i].uv.y), node = 0, count = uint(instances[i].uv.w);
        [loop] while (node < count)
        {
            InstanceData box = instances[root + node];
            if (!nearestHitBox(box.baseColor.xyz, box.material.xyz, o, reciprocal, parallel, distance))
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
                float candidateDistance;
                float2 candidateBary;
                if (!intersectSceneTriangle(tri, o, d, distance, materialBase, false, candidateDistance, candidateBary))
                    continue;
                distance       = candidateDistance;
                hitBary        = candidateBary;
                hitInstance    = i;
                hitTriangle    = index;
                found          = true;
            }
            node = uint(box.baseColor.w);
        }
    }
    return found;
}
float hybridFrameRotation(uint2 pixel, float frame)
{
    if (frame < .5)
        return 0;
    uint seed = pixel.x * 1973u + pixel.y * 9277u + uint(frame) * 26699u;
    seed      = (seed ^ (seed >> 16)) * 2246822519u;
    seed ^= seed >> 13;
    return (seed & 65535u) * (6.28318530718 / 65536.0);
}
float hybridRotation(uint2 pixel)
{
    return hybridFrameRotation(pixel, irradiance[6].w);
}
float3 hybridSampleProbes(float3 position, float3 normal, bool skyLighting, out float coverage);
float3 probeDiffuseRadiance(SurfaceData surface, float3 view, float3 irradiance)
{
    float nv = saturate(dot(surface.normal, view));
    float3 f0 = lerp(dielectricF0(surface.specular), surface.base, surface.metallic);
    float3 f = fresnel(nv, f0, lerp(surface.specular.a, 1, surface.metallic));
    return irradiance * saturate(surface.base) * (1 - surface.metallic)
           * (1 - diffuseFresnel(nv, f, surface.specular)) * surface.ao
           * clearcoatTransmission(surface, view);
}
float3 hybridHitRadiance(uint instance, uint hitTri, float2 bary, float3 position, float3 ray,
                         float probeFeedback = 0)
{
    InstanceData tri = instances[hitTri];
    InstanceData mat = instances[uint(instances[instance].uv.x) + uint(tri.emissive.w)];
    float2 uv0 = tri.uv.xy, uv1 = tri.uv.zw;
    float2 uv2 = float2(tri.normalMatrix[0][0], tri.normalMatrix[1][1]);
    float2 uv1a = float2(tri.world._m00, tri.world._m10);
    float2 uv1b = float2(tri.world._m20, tri.world._m30);
    float2 uv1c = float2(tri.world._m01, tri.world._m11);
    float2 tex0 = uv0 + (uv1 - uv0) * bary.x + (uv2 - uv0) * bary.y;
    float2 tex1 = uv1a + (uv1b - uv1a) * bary.x + (uv1c - uv1a) * bary.y;
    float2 uv   = frac(tex0) * mat.uv.xy + mat.uv.zw;
    float2 aoUV = mat.world._m21 > .5 ? frac(tex1) * mat.uv.xy + mat.uv.zw : uv;
    float3 localEdge1 = tri.material.xyz - tri.baseColor.xyz;
    float3 localEdge2 = tri.emissive.xyz - tri.baseColor.xyz;
    float3 worldNormal = normalize(mul(
      (float3x3)instances[instance].normalMatrix, cross(localEdge1, localEdge2)));
    if (dot(worldNormal, ray) > 0)
        worldNormal = -worldNormal;

    SurfaceData hit = (SurfaceData)0;
    hit.position = position;
    hit.specular = materialSpecular(mat);
    uint specularMaps = uint(mat.world._m22);
    float2 atlasUV1 = frac(tex1) * mat.uv.xy + mat.uv.zw;
    if (specularMaps & 1u)
        hit.specular.a *= normalTexture.SampleLevel(textureSampler, specularMaps & 4u ? atlasUV1 : uv, 0).a;
    if (specularMaps & 2u)
        hit.specular.rgb *= occlusionTexture.SampleLevel(textureSampler, specularMaps & 8u ? atlasUV1 : uv, 0).gba;
    hit.sheen = materialSheen(mat) * sampleSheenMaps(mat, uv, atlasUV1);
    hit.film = materialFilm(mat, sampleFilmMaps(mat, uv, atlasUV1));
    hit.clearcoat = materialClearcoat(mat);
    hit.normal = hit.geometricNormal = hit.coatNormal = worldNormal;
    if (hit.clearcoat.x > 0)
    {
        CoatUVs coatUV = clearcoatUVs(mat, tex0, tex1);
        if (coatUV.mask & 1u)
            hit.clearcoat.x *= coatStrengthTexture.SampleLevel(textureSampler,
                frac(coatUV.strength) * mat.uv.xy + mat.uv.zw, 0).r;
        if (coatUV.mask & 2u)
            hit.clearcoat.y *= coatRoughnessTexture.SampleLevel(textureSampler,
                frac(coatUV.roughness) * mat.uv.xy + mat.uv.zw, 0).g;
        if (coatUV.mask & 4u)
        {
            bool uvSet = uint(mat.world._m02) & 4u;
            float3 coatMap = coatNormalTexture.SampleLevel(textureSampler,
                frac(coatUV.normal) * mat.uv.xy + mat.uv.zw, 0).xyz * 2 - 1;
            hit.coatNormal = applyCoatNormal(worldNormal, coatMap, coatUV.scale,
                mul((float3x3)instances[instance].world, localEdge1),
                mul((float3x3)instances[instance].world, localEdge2),
                uvSet ? uv1b - uv1a : uv1 - uv0, uvSet ? uv1c - uv1a : uv2 - uv0);
        }
    }
    float2 deltaUV1 = independentTextureUV(mat, uv1 - uv0, 1u, true), deltaUV2 = independentTextureUV(mat, uv2 - uv0, 1u, true);
    float2 normalAtlasUV = frac(independentTextureUV(mat, tex0, 1u)) * mat.uv.xy + mat.uv.zw;
    float  uvDet = deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x;
    float3 hitTangent = 0, hitBitangent = 0;
    if (abs(uvDet) > 1e-8)
    {
        float3 edge1 = mul((float3x3)instances[instance].world, localEdge1);
        float3 edge2 = mul((float3x3)instances[instance].world, localEdge2);
        // The determinant sign preserves the orientation of mirrored UV charts.
        float3 t = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * sign(uvDet);
        float3 b = (edge2 * deltaUV1.x - edge1 * deltaUV2.x) * sign(uvDet);
        float  scale = rsqrt(max(max(dot(t, t), dot(b, b)), 1e-12));
        hitTangent = t;
        hitBitangent = b;
        float3 sampled = normalTexture.SampleLevel(textureSampler, normalAtlasUV, 0).xyz * 2 - 1;
        sampled.xy *= mat.material.z;
        float3 mapped = (t * sampled.x + b * sampled.y) * scale + worldNormal * sampled.z;
        if (dot(mapped, mapped) > 1e-12)
            hit.normal = normalize(mapped);
    }
    hit.anisotropy = anisotropicFrame(materialAnisotropy(mat, uv, atlasUV1), hit.normal, hitTangent, hitBitangent);
    hit.base      = baseTexture.SampleLevel(textureSampler, wrapBaseUV(tex0, mat.emissive.w) * mat.uv.xy + mat.uv.zw, 0).rgb * mat.baseColor.rgb * triangleColor(tri, bary).rgb;
    if ((uint(mat.emissive.w) & 8u) != 0)
        return hit.base;
    float2 mrUV = frac(independentTextureUV(mat, tex0, 2u)) * mat.uv.xy + mat.uv.zw;
    float4 mr = materialTexture.SampleLevel(textureSampler, mrUV, 0);
    hit.metallic  = saturate(mr.b * mat.material.x);
    hit.roughness = clamp(mr.g * mat.material.y, .04, 1);
    hit.ao        = lerp(1, occlusionTexture.SampleLevel(textureSampler, aoUV, 0).r, mat.material.w);
    float2 emissionUV = (uint(mat.emissive.w) & 16u) ? frac(tex1) * mat.uv.xy + mat.uv.zw : uv;
    hit.emissive = emissiveTexture.SampleLevel(textureSampler, emissionUV, 0).rgb * mat.emissive.rgb;
    float coverage = 0;
    float3 bounce = 0;
    if (probeFeedback > 0 && emissiveFactor.w > .5 && hit.metallic < 1)
    {
        float3 cached = hybridSampleProbes(hit.position, hit.normal, true, coverage);
        // Conservative feedback limits accumulation through light leaks in the coarse grid.
        bounce = probeFeedback * probeDiffuseRadiance(hit, -ray, cached);
    }
    return surfaceRadiance(hit, 0, 0, -ray, false, true, coverage) + bounce;
}

// Two buffers keep the previous update available while writing the next batch.
struct HybridProbeLobe
{
    float4 irradiance; // RGB and validity
    float4 distanceMoments; // Normalized mean and second moment
};
[[vk::binding(25, 2)]] cbuffer HybridProbeUpdate : register(b0, space2)
{
    float4 probeOrigin; // XYZ origin, W enables octahedral storage
    float4 probeSpacingDistance; // XYZ spacing, W ray limit
    uint4 probeDimensionsFirst; // XYZ counts, W first probe in this batch
    uint2 probeBatch; // Count and update sequence
    float probeHistoryWeight;
    uint probeSceneHeader;
};
[[vk::binding(26, 2)]] StructuredBuffer<HybridProbeLobe> probePrevious : register(t0, space2);
[[vk::binding(27, 2)]] RWStructuredBuffer<HybridProbeLobe> probeUpdated : register(u0, space2);

#if defined(NOVA_SHARED_PROBE_RAYS)
groupshared float4 directionDistance[96];
groupshared float3 sampleRadiance[96];

[numthreads(96, 1, 1)]
void HybridProbeUpdateCS(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    const uint sampleCount = 96;
    // Dispatch one group per scheduled probe. Every lane reaches the barrier.
    uint3 dimensions = probeDimensionsFirst.xyz;
    uint probe = probeDimensionsFirst.w + group.x;
    uint3 cell = uint3(probe % dimensions.x, probe / dimensions.x % dimensions.y,
                      probe / (dimensions.x * dimensions.y));
    float3 origin = probeOrigin.xyz + float3(cell) * probeSpacingDistance.xyz;
    float phase = frac(float(probeBatch.y) * .61803399 + float(probe) * .75487767);
    float z = 1 - 2 * frac((lane + .5) / sampleCount + phase);
    float phi = lane * 2.39996323 + phase * 6.28318531;
    float radius = sqrt(max(0, 1 - z * z));
    float3 ray = float3(radius * cos(phi), z, radius * sin(phi));
    float distance = probeSpacingDistance.w;
    uint instance, hitTriangle;
    float2 bary;
    float3 radiance = 0;
    bool sampleSky = cameraForward.w > .5 && instances[probeSceneHeader].world._m22 > .5;
    bool feedback = sampleSky && instances[probeSceneHeader].world._m32 > .5;
    if (hybridNearest(probeSceneHeader, origin, ray, distance, instance, hitTriangle, bary))
        radiance = hybridHitRadiance(instance, hitTriangle, bary, origin + ray * distance, ray, feedback ? .65 : 0);
    else if (sampleSky)
        radiance = environmentTexture.SampleLevel(textureSampler, environmentDirection(ray), 0).rgb * environmentParams.z;
    directionDistance[lane] = float4(ray, saturate(distance / probeSpacingDistance.w));
    sampleRadiance[lane] = radiance;
    GroupMemoryBarrierWithGroupSync();

    if (probeOrigin.w > .5)
    {
        // The first 64 records hold irradiance. All 256 hold directional depth.
        for (uint bin = lane; bin < 256; bin += sampleCount)
        {
            float3 depthNormal = octDecode((float2(bin % 16, bin / 16) + .5) / 8 - 1);
            float3 diffuseNormal = octDecode((float2(bin % 8, bin / 8) + .5) / 4 - 1);
            float2 moments = 0;
            float3 incoming = 0;
            float depthWeight = 0, diffuseWeight = 0;
            [loop] for (uint i = 0; i < sampleCount; ++i)
            {
                float4 sample = directionDistance[i];
                float weight = pow(saturate(dot(depthNormal, sample.xyz)), 12);
                moments += float2(sample.w, sample.w * sample.w) * weight;
                depthWeight += weight;
                if (bin < 64)
                {
                    float w = saturate(dot(diffuseNormal, sample.xyz));
                    incoming += sampleRadiance[i] * w;
                    diffuseWeight += w;
                }
            }
            uint index = probe * 256 + bin;
            HybridProbeLobe previous = probePrevious[index];
            float history = saturate(probeHistoryWeight) * previous.irradiance.w;
            HybridProbeLobe result;
            result.irradiance = float4(lerp(incoming / max(diffuseWeight, 1e-5), previous.irradiance.rgb, history), 1);
            result.distanceMoments = float4(lerp(moments / max(depthWeight, 1e-5), previous.distanceMoments.xy, history), 0, 0);
            probeUpdated[index] = result;
        }
        return;
    }

    if (lane >= 6)
        return;
    uint axis = lane / 2;
    float3 n = float3(axis == 0, axis == 1, axis == 2) * ((lane & 1) ? -1 : 1);
    float3 incoming = 0;
    float2 moments = 0;
    float sum = 0;
    [loop] for (uint j = 0; j < sampleCount; ++j)
    {
        float w = saturate(dot(n, directionDistance[j].xyz));
        float d = directionDistance[j].w;
        incoming += sampleRadiance[j] * w;
        moments += float2(d, d * d) * w;
        sum += w;
    }
    uint index = probe * 6 + lane;
    HybridProbeLobe previous = probePrevious[index];
    float history = saturate(probeHistoryWeight) * previous.irradiance.w;
    HybridProbeLobe result;
    result.irradiance = float4(lerp(incoming / max(sum, 1e-5), previous.irradiance.rgb, history), 1);
    result.distanceMoments = float4(lerp(moments / max(sum, 1e-5), previous.distanceMoments.xy, history), 0, 0);
    probeUpdated[index] = result;
}
#else
[numthreads(32, 1, 1)]
void HybridProbeUpdateCS(uint3 thread : SV_DispatchThreadID)
{
    const uint lobeCount = 6, sampleCount = 16;
    uint localProbe = thread.x / lobeCount;
    if (localProbe >= probeBatch.x)
        return;

    uint3 dimensions = probeDimensionsFirst.xyz;
    uint probe = probeDimensionsFirst.w + localProbe;
    if (probe >= dimensions.x * dimensions.y * dimensions.z)
        return;

    uint3 cell = uint3(
      probe % dimensions.x,
      probe / dimensions.x % dimensions.y,
      probe / (dimensions.x * dimensions.y));
    float3 origin = probeOrigin.xyz + float3(cell) * probeSpacingDistance.xyz;
    uint lobe = thread.x % lobeCount;
    uint axis = lobe / 2;
    float3 n = float3(axis == 0, axis == 1, axis == 2) * ((lobe & 1) ? -1 : 1);
    float3 tangent = normalize(cross(abs(n.y) < .99 ? float3(0, 1, 0) : float3(1, 0, 0), n));
    float3 bitangent = cross(n, tangent);
    float rotation = frac(float(probeBatch.y) * .61803399 + float(probe) * .75487767) * 6.28318531;
    float3 incoming = 0;
    float2 moments = 0;
    bool sampleSky = cameraForward.w > .5 && instances[probeSceneHeader].world._m22 > .5;
    bool probeFeedback = sampleSky && instances[probeSceneHeader].world._m32 > .5;
    [loop] for (uint j = 0; j < sampleCount; ++j)
    {
        float u = (j + .5) / sampleCount, phi = rotation + j * 2.39996323;
        float3 ray = sqrt(u) * (tangent * cos(phi) + bitangent * sin(phi)) + n * sqrt(1 - u);
        float distance = probeSpacingDistance.w;
        uint instance, hitTriangle;
        float2 bary;
        if (hybridNearest(probeSceneHeader, origin, ray, distance, instance, hitTriangle, bary))
            incoming += hybridHitRadiance(instance, hitTriangle, bary, origin + ray * distance, ray, probeFeedback ? .65 : 0);
        else if (sampleSky)
            incoming += environmentTexture.SampleLevel(
              textureSampler, environmentDirection(ray), 0).rgb * environmentParams.z;
        float normalizedDistance = saturate(distance / probeSpacingDistance.w);
        moments += float2(normalizedDistance, normalizedDistance * normalizedDistance);
    }
    uint index = probe * lobeCount + lobe;
    HybridProbeLobe previous = probePrevious[index];
    float history = saturate(probeHistoryWeight) * previous.irradiance.w;
    HybridProbeLobe result;
    result.irradiance = float4(lerp(incoming / sampleCount, previous.irradiance.rgb, history), 1);
    result.distanceMoments = float4(lerp(moments / sampleCount, previous.distanceMoments.xy, history), 0, 0);
    probeUpdated[index] = result;
}

#endif

float4 hybridOctahedralTap(uint probe, int2 tap, int side, bool distance)
{
    // Across a folded edge the other coordinate reverses. This also remaps corners.
    if (tap.x < 0 || tap.x >= side)
        tap = int2(clamp(tap.x, 0, side - 1), side - 1 - tap.y);
    if (tap.y < 0 || tap.y >= side)
        tap = int2(side - 1 - tap.x, clamp(tap.y, 0, side - 1));
    HybridProbeLobe bin = probePrevious[probe * 256 + uint(tap.y * side + tap.x)];
    return distance ? bin.distanceMoments : bin.irradiance;
}

// Axis lobes use squared direction components so constant radiance stays constant.
float4 hybridProbeDirection(uint probe, float3 direction, bool distance)
{
    if (probeOrigin.w > .5)
    {
        int side = distance ? 16 : 8;
        float2 at = (octEncode(direction) * .5 + .5) * side - .5;
        int2 first = int2(floor(at));
        float2 f = frac(at);
        return lerp(
          lerp(hybridOctahedralTap(probe, first, side, distance),
               hybridOctahedralTap(probe, first + int2(1, 0), side, distance), f.x),
          lerp(hybridOctahedralTap(probe, first + int2(0, 1), side, distance),
               hybridOctahedralTap(probe, first + 1, side, distance), f.x), f.y);
    }
    float3 weights = direction * direction;
    uint first = probe * 6;
    HybridProbeLobe x = probePrevious[first     + uint(direction.x < 0)];
    HybridProbeLobe y = probePrevious[first + 2 + uint(direction.y < 0)];
    HybridProbeLobe z = probePrevious[first + 4 + uint(direction.z < 0)];
    if (distance)
        return x.distanceMoments * weights.x + y.distanceMoments * weights.y + z.distanceMoments * weights.z;
    return x.irradiance * weights.x + y.irradiance * weights.y + z.irradiance * weights.z;
}

float hybridProbeVisibility(float normalizedDistance, float2 moments)
{
    float delta = max(normalizedDistance - moments.x, 0);
    float variance = max(moments.y - moments.x * moments.x, 1e-5);
    float bound = variance / (variance + delta * delta);
    // Squaring the upper bound suppresses broad variance tails behind walls.
    return bound * bound;
}

float3 hybridSampleProbes(float3 position, float3 normal, bool skyLighting, out float coverage)
{
    coverage = 0;
    uint3 dimensions = probeDimensionsFirst.xyz;
    if (any(dimensions == 0))
        return 0;
    float3 rel = (position - probeOrigin.xyz) / probeSpacingDistance.xyz;
    float3 edge = min(rel + .5, float3(dimensions) - .5 - rel);
    float edgeDistance = min(edge.x, min(edge.y, edge.z));
    if (edgeDistance <= 0)
        return 0;
    // Fade from the outermost probe centers to the boundary of the volume.
    float boundaryWeight = smoothstep(0, .5, edgeDistance);

    float3 grid = clamp(rel, 0, float3(dimensions - 1));
    uint3 first = uint3(floor(grid));
    float3 fraction = frac(grid);
    float3 total = 0;
    float weightTotal = 0, facingTotal = 0;
    [loop] for (uint corner = 0; corner < 8; ++corner)
    {
        uint3 offset = uint3(corner & 1, (corner >> 1) & 1, corner >> 2);
        float3 axisWeight = lerp(1 - fraction, fraction, float3(offset));
        float weight = axisWeight.x * axisWeight.y * axisWeight.z;
        if (weight == 0)
            continue;

        uint3 cell = first + offset;
        uint probe = cell.x + dimensions.x * (cell.y + dimensions.y * cell.z);
        float4 irradiance = hybridProbeDirection(probe, normal, false);
        if (irradiance.w <= 0)
            continue;

        float3 center = probeOrigin.xyz + float3(cell) * probeSpacingDistance.xyz;
        float3 toSurface = position - center;
        float distance = length(toSurface);
        float3 direction = distance > 1e-5 ? toSurface / distance : normal;
        float2 moments = hybridProbeDirection(probe, direction, true).xy;
        float visibility = hybridProbeVisibility(distance / probeSpacingDistance.w, moments);
        float facing = saturate(.5 - .5 * dot(normal, direction));
        total += irradiance.rgb * (weight * visibility * facing);
        // Keep blocked probes in the denominator so normalization cannot restore leaked light.
        weightTotal += weight * irradiance.w;
        facingTotal += weight * irradiance.w * facing;
    }
    coverage = boundaryWeight * saturate(weightTotal);
    if (skyLighting)
        return facingTotal > 0 ? total * (coverage / facingTotal) : 0;
    return weightTotal > 0 ? total * (boundaryWeight / weightTotal) : 0;
}

#if defined(NOVA_PROBE_SAMPLE_VALIDATION)
[[vk::binding(3, 2)]] StructuredBuffer<float4> probeQueries : register(t1, space2);
[[vk::binding(4, 2)]] RWStructuredBuffer<float4> probeSamples : register(u1, space2);
[numthreads(32, 1, 1)]
void HybridProbeSampleCS(uint3 thread : SV_DispatchThreadID)
{
    if (thread.x < probeBatch.x)
    {
        float coverage;
        float3 irradiance = hybridSampleProbes(
          probeQueries[thread.x * 2].xyz, probeQueries[thread.x * 2 + 1].xyz, false, coverage);
        probeSamples[thread.x] = float4(irradiance, 1);
    }
}
#endif

float3 hybridDiffuseBounce(SurfaceData surface, uint header, float rotation)
{
    float3 n = surface.geometricNormal;
    float3 t = normalize(cross(abs(n.y) < .99 ? float3(0, 1, 0) : float3(1, 0, 0), n));
    float3 b = cross(n, t);
    float3 origin = surface.position + n * max(1e-4, instances[header].uv.x);
    float3 incoming = 0;
    const uint samples = 4;
    [loop] for (uint j = 0; j < samples; ++j)
    {
        float u = (j + .5) / samples, phi = j * 2.39996323 + rotation;
        float3 ray = sqrt(u) * (t * cos(phi) + b * sin(phi)) + n * sqrt(1 - u);
        float distance = instances[header].world._m31;
        uint instance, hitTriangle;
        float2 bary;
        // The existing environment lighting already accounts for rays that leave the scene.
        if (hybridNearest(header, origin, ray, distance, instance, hitTriangle, bary))
            incoming += hybridHitRadiance(instance, hitTriangle, bary, origin + ray * distance, ray);
    }
    return incoming * surface.base * (1 - surface.metallic) / samples;
}

bool hybridMirror(uint header, float roughness)
{
    return instances[header].world._m13 > .5 && roughness <= .05002;
}

float3 hybridReflectionLobe(SurfaceData primary, uint header, float3 v, float3 n, float rough,
                            float3 f0, float f90, float rotation, bool baseLayer, bool measureHit, out float hitDistance, out bool filteredByProbe)
{
    hitDistance = 0;
    filteredByProbe = false;
#if defined(NOVA_HYBRID_PROBES)
    if (rough >= .8 && probeDimensionsFirst.x > 0 && cameraForward.w > .5
        && instances[header].world._m23 > .5)
    {
        float coverage;
        float3 reflected = reflect(-v, n);
        float3 incident = hybridSampleProbes(primary.position, reflected, true, coverage);
        if (coverage >= .999)
        {
            filteredByProbe = true;
            float2 brdf = environmentBRDF(rough, saturate(dot(n, v)));
            float3 reflectance = baseLayer && primary.film.x > 0
              ? materialReflectance(saturate(dot(n, v)), primary.base, primary.metallic, primary.specular, primary.film)
                * (brdf.x + brdf.y)
              : f0 * brdf.x + f90 * brdf.y;
            return incident * reflectance;
        }
    }
#endif
    float  maxDistance = instances[header].world._m01;
    bool mirror = hybridMirror(header, rough);
    uint samples = mirror ? 1 : uint(instances[header].world._m11);
    float3 tangent   = normalize(cross(abs(n.y) < .99 ? float3(0, 1, 0) : float3(1, 0, 0), n)),
           bitangent = cross(n, tangent);
    bool aniso = baseLayer && primary.anisotropy.w > 0;
    float2 anisotropyRoughness = anisotropicRoughness(rough, primary.anisotropy.w);
    float  nv = max(.001, dot(n, v)), a = rough * rough;
    float3 sum = 0;
    float nearest = maxDistance, farthest = 0;
    uint hitCount = 0, firstInstance = 0;
    bool sameInstance = true;
    [loop] for (uint j = 0; j < samples; ++j)
    {
        float3 h = n;
        if (!mirror)
        {
            // Decorrelate elevation from azimuth while keeping the midpoint on static frames.
            float elevation = abs(irradiance[6].w) >= .5 ? frac(rotation * 17 + .5) : .5;
            float u = (j + elevation) / samples, phi = j * 2.39996323 + rotation;
            float cosTheta = sqrt((1 - u) / (1 + (a * a - 1) * u));
            float sinTheta = sqrt(max(0, 1 - cosTheta * cosTheta));
            h = aniso ? anisotropicHalfVector(float2(u, frac(phi / 6.283185307)), n,
              primary.anisotropy.xyz, anisotropyRoughness)
              : normalize(tangent * (sinTheta * cos(phi)) + bitangent * (sinTheta * sin(phi)) + n * cosTheta);
        }
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
            if (measureHit)
            {
                if (hitCount == 0)
                    firstInstance = instance;
                sameInstance = sameInstance && firstInstance == instance;
                nearest = min(nearest, distance);
                farthest = max(farthest, distance);
                hitDistance += distance;
                ++hitCount;
            }
            float probeWeight = 0;
#if defined(NOVA_HYBRID_PROBES)
            if (probeDimensionsFirst.x > 0 && cameraForward.w > .5 && instances[header].world._m03 > .5)
                probeWeight = 1;
#endif
            radiance = hybridHitRadiance(instance, hitTri, bary, origin + ray * distance, ray, probeWeight);
        }
        else
            radiance =
              cameraForward.w > .5
                ? environmentTexture.SampleLevel(textureSampler, environmentDirection(ray), 0).rgb * environmentParams.z
                : studio(ray, 0);
        float3 reflectance = baseLayer && primary.film.x > 0
          ? materialReflectance(vh, primary.base, primary.metallic, primary.specular, primary.film)
          : fresnel(vh, f0, f90);
        if (mirror)
        {
            sum = radiance * reflectance;
            break;
        }
        float k = pow(rough + 1, 2) / 8;
        float g = (nv / (nv * (1 - k) + k)) * (nl / (nl * (1 - k) + k));
        if (aniso)
            g = 4 * nv * nl * anisotropicVisibility(v, ray, n, primary.anisotropy.xyz, anisotropyRoughness);
        sum += radiance * reflectance * (g * vh / (nv * nh));
    }
    // Mixed sky/geometry or materially different hit distances have no stable virtual point.
    hitDistance = sameInstance && hitCount == samples && farthest - nearest <= .15 * nearest
                    ? hitDistance / max(1, hitCount) : 0;
    return sum / max(1, samples);
}

float3 hybridReflection(SurfaceData s, float rotation, uint header, bool traceCoat, bool measureHit, out float hitDistance, out bool skipDenoise)
{
    float3 v = cameraUp.w < 0 ? -cameraForward.xyz : normalize(eyeExposure.xyz - s.position);
    float3 reflection = 0;
    hitDistance = 0;
    const int lobes = traceCoat && s.clearcoat.x > 0 ? 2 : 1;
    skipDenoise = true;
    [loop] for (int lobe = 0; lobe < lobes; ++lobe)
    {
        bool coat = lobe != 0;
        float3 f0 = coat ? .04.xxx : lerp(dielectricF0(s.specular), s.base, s.metallic);
        float f90 = coat ? 1 : lerp(s.specular.a, 1, s.metallic);
        float3 n = coat ? s.coatNormal : s.normal;
        float roughness = max(.02, coat ? s.clearcoat.y : s.roughness);
        float weight = coat ? s.clearcoat.x : traceCoat ? clearcoatTransmission(s, v) : 1;
        float distance;
        bool filteredByProbe;
        reflection += weight * hybridReflectionLobe(
          s, header, v, n, roughness, f0, f90, rotation + lobe * 1.618, !coat,
          measureHit && lobes == 1 && roughness <= .2 && dot(n, s.geometricNormal) > .995,
          distance, filteredByProbe);
        skipDenoise = skipDenoise && (filteredByProbe || hybridMirror(header, roughness));
        if (!coat)
            hitDistance = distance;
    }
    return reflection;
}

float4 HybridAOPSMain(SkyOut input) : SV_Target
{
    uint width, height;
    gBase.GetDimensions(width, height);
    int2 extent = int2(width, height) - 2;
    int2 local = int2(input.position.xy) - 1;
    if (any(local < 0) || any(local >= (extent + 1) / 2))
        return 0;
    int3 pixel = int3(local * 2 + 1, 0);
    if (gBase.Load(pixel).w <= 0)
        return 0;
    input.uv = (float2(local * 2) + .5) / float2(extent);
    float2 ndc = input.uv * float2(2, -2) + float2(-1, 1);
    input.direction = cameraForward.xyz + ndc.x * cameraRight.w * cameraRight.xyz
                      + ndc.y * cameraUp.w * cameraUp.xyz;
    float3 normal = octDecode(gNormals.Load(pixel).zw);
    float3 position = surfacePosition(input, gPosition.Load(pixel).xy);
    float angle = hybridFrameRotation(uint2(pixel.xy), -skyParams.z);
#if defined(NOVA_HYBRID_BLUE_NOISE)
    if (irradiance[3].w > .5 && (uint(instances[uint(irradiance[3].w) - 1].world._m33) & 1u) != 0)
        angle = novaBluePhase(uint2(local), uint(-skyParams.z - 1));
#endif
    return float4(hybridAO(position, normal, angle), 0, 0, 1);
}

float4 HybridReflectionPSMain(SkyOut input) : SV_Target
{
    uint width, height;
    gBase.GetDimensions(width, height);
    int2 extent = int2(width, height) - 2;
    int2 local = int2(input.position.xy) - 1;
    if (any(local < 0) || any(local >= (extent + 1) / 2))
        return 0;
    // The AA attachments reserve a one-pixel border around the active viewport.
    int3 pixel = int3(local * 2 + 1, 0);
    float4 base = gBase.Load(pixel);
    if (base.w <= 0)
        return 0;
    float4 normals = gNormals.Load(pixel), pos = gPosition.Load(pixel);
    // Convert the half-resolution pixel center to the representative full-resolution center.
    input.uv = (input.uv * float2((extent + 1) / 2) * 2 - .5) / float2(extent);
    float2 ndc = float2(input.uv.x * 2 - 1, 1 - input.uv.y * 2);
    input.direction = cameraForward.xyz + ndc.x * cameraRight.w * cameraRight.xyz
                      + ndc.y * cameraUp.w * cameraUp.xyz;
    SurfaceData s = (SurfaceData)0;
    s.base = base.rgb;
    s.roughness = base.w;
    s.normal = octDecode(normals.xy);
    s.geometricNormal = octDecode(normals.zw);
    s.position = surfacePosition(input, pos.xy);
    s.metallic = gEmission.Load(pixel).w;
    s.specular = 1;
    if (pos.w > .5 && irradiance[3].w > pos.w)
        deferredMaterialLayers(s, instances[uint(irradiance[3].w - 1 - pos.w)], pixel);
    float4 coat = gClearcoat.Load(pixel);
    s.coatNormal = octDecode(coat.xy);
    s.clearcoat = coat.zw;
    uint header = uint(irradiance[3].w) - 1;
    bool traceCoat = instances[header].world._m02 > .5;
    float hitDistance;
    bool skipDenoise;
    float rotation = hybridFrameRotation(uint2(pixel.xy), abs(irradiance[6].w));
#if defined(NOVA_HYBRID_BLUE_NOISE)
    if (uint(instances[header].world._m33) & 4u)
        rotation = novaBluePhase(uint2(local) + uint2(13, 3), uint(max(0, abs(irradiance[6].w) - 1)));
#endif
    float3 reflected = hybridReflection(
      s, rotation, header, traceCoat,
      instances[header].world._m12 > .5 && cameraUp.w >= 0, hitDistance, skipDenoise);
    // Positive alpha below 1 marks a valid sample that bypasses reflection denoising.
    return float4(reflected, skipDenoise ? .5 : 1 + log2(1 + hitDistance));
}

#if defined(NOVA_HALF_REFLECTIONS)
[[vk::binding(32, 1)]] Texture2D<float4> hybridReflectionTexture : register(t32);
bool upsampleHybridReflection(int2 pixel, float4 position, SurfaceData surface, bool traceCoat, out float3 reflection)
{
    uint width, height;
    hybridReflectionTexture.GetDimensions(width, height);
    int2 last = int2(width, height) - 2;
    float2 low = float2(pixel - 1) * .5 + 1;
    int2 first = int2(low);
    float2 fraction = frac(low);
    float3 radiance = 0;
    float weightSum = 0;
    float3 brightest = 0;
    float strongest = 0, nextStrongest = 0, brightestWeight = 0;
    uint compatible = 0;
    bool suppressOutliers = surface.roughness >= .25;
    [unroll] for (int y = 0; y < 2; ++y)
        [unroll] for (int x = 0; x < 2; ++x)
        {
            int2 tap = min(first + int2(x, y), last);
            float4 sampleValue = hybridReflectionTexture.Load(int3(tap, 0));
            int3 guide = int3(tap * 2 - 1, 0);
            float4 guideDepth = gPosition.Load(guide), guideNormal = gNormals.Load(guide);
            float dz = abs((position.x - guideDepth.x) + (position.y - guideDepth.y));
            float alignment = min(dot(surface.normal, octDecode(guideNormal.xy)),
                                  dot(surface.geometricNormal, octDecode(guideNormal.zw)));
            if (traceCoat)
            {
                float4 coat = gClearcoat.Load(guide);
                if (abs(coat.z - surface.clearcoat.x) > .1)
                    continue;
                if (surface.clearcoat.x > 0)
                {
                    if (abs(coat.w - surface.clearcoat.y) > .1)
                        continue;
                    alignment = min(alignment, dot(surface.coatNormal, octDecode(coat.xy)));
                }
            }
            float weight = (sampleValue.a > 0 ? 1 : 0) * exp2(-120 * dz) * pow(saturate(alignment), 24);
            if (dz > .06 || alignment < .8 || abs(position.w - guideDepth.w) > .5)
                weight = 0;
            weight *= max(.02, (x == 0 ? 1 - fraction.x : fraction.x) * (y == 0 ? 1 - fraction.y : fraction.y));
            if (suppressOutliers && weight > .001)
            {
                ++compatible;
                float brightness = dot(sampleValue.rgb, float3(.2126, .7152, .0722));
                if (brightness > strongest)
                {
                    nextStrongest = strongest;
                    strongest = brightness;
                    brightest = sampleValue.rgb;
                    brightestWeight = weight;
                }
                else
                    nextStrongest = max(nextStrongest, brightness);
            }
            radiance += sampleValue.rgb * weight;
            weightSum += weight;
        }
    if (compatible >= 3)
    {
        // Limit an isolated bright tap while retaining the existing depth and normal weights.
        float ceiling = max(.015, nextStrongest * 1.5);
        radiance -= brightest * brightestWeight * (1 - saturate(ceiling / max(strongest, .0001)));
    }
    reflection = max(radiance, 0) / max(weightSum, .001);
    return weightSum >= .001;
}
#endif

// Bilateral reconstruction on the full-resolution receiver surface. When the
// coarse grid misses a thin receiver, the lighting pass traces that pixel.
void upsampleRasterShadows(inout SurfaceData s, int2 pixel, float logDepth, float3 normal)
{
    uint width, height;
    hybridShadow0.GetDimensions(width, height);
    int2 last = int2(width, height) - 2;
    float2 samplePosition = float2(pixel - 1) * .5;
    int2 first = int2(floor(samplePosition)) + 1;
    float2 fraction = frac(samplePosition);
    float4 visibility0 = 0, visibility1 = 0, visibility2 = 0, visibility3 = 0;
    float total = 0;
    uint header = uint(irradiance[3].w) - 1;
    uint count = instances[header].uv.w > .5 ? uint(instances[header].uv.y) : 1;
    [unroll] for (int y = 0; y < 2; y++)
        [unroll] for (int x = 0; x < 2; x++)
        {
            int2 tap = min(first + int2(x, y), last);
            int3 guide = int3((tap - 1) * 2 + 1, 0);
            float2 depth = gPosition.Load(guide).xy;
            float dz = abs(depth.x + depth.y - logDepth);
            float agreement = dot(normal, octDecode(gNormals.Load(guide).zw));
            if (gBase.Load(guide).w <= 0 || dz >= .025 || agreement < .9)
                continue;
            float2 bilinear = lerp(1 - fraction, fraction, float2(x, y));
            float weight = (bilinear.x * bilinear.y + .02) * (1 - dz / .025) * agreement;
            total += weight;
            visibility0 += hybridShadow0.Load(int3(tap, 0)) * weight;
            if (count > 4) visibility1 += hybridShadow1.Load(int3(tap, 0)) * weight;
            if (count > 8) visibility2 += hybridShadow2.Load(int3(tap, 0)) * weight;
            if (count > 12) visibility3 += hybridShadow3.Load(int3(tap, 0)) * weight;
        }
    if (total < .002)
        return;
    s.useVisibility = true;
    s.visibility0 = visibility0 / total;
    s.visibility1 = visibility1 / total;
    s.visibility2 = visibility2 / total;
    s.visibility3 = visibility3 / total;
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
    if (irradiance[1].w > .5)
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
    if (irradiance[1].z > .5 && irradiance[3].w > .5)
        upsampleRasterShadows(s, pixel.xy, pos.x + pos.y, s.geometricNormal);
    s.position        = surfacePosition(input, pos.xy);
    s.ao              = max(0, pos.z);
    s.alphaInfo       = pos.z < 0 ? 8 : 0;
    if (debug == 0 && s.alphaInfo > 0)
        return shadeSurface(s);
    s.emissive        = emission.rgb;
    s.metallic        = emission.w;
    s.specular        = 1;
    s.sheen           = 0;
    s.film            = 0;
    s.anisotropy      = 0;
    float4 coat       = gClearcoat.Load(pixel);
    s.coatNormal      = octDecode(coat.xy);
    s.clearcoat       = coat.zw;
    float reflectionOverride = -1;
    if (pos.w > .5 && irradiance[3].w > pos.w)
    {
        InstanceData material = instances[uint(irradiance[3].w - 1 - pos.w)];
        deferredMaterialLayers(s, material, pixel);
        reflectionOverride = material.world._m32;
    }
    if (irradiance[1].w > .5)
    {
        // The independent AO pass has already multiplied material occlusion in the position attachment.
        float ao = skyParams.z < -.5 ? 1 : hybridAO(s.position, s.geometricNormal, hybridRotation(uint2(input.position.xy)));
        if (irradiance[3].w > .5 && abs(instances[uint(irradiance[3].w) - 1].world._m30 - 2) < .1)
            return float4(diagnosticLinear(ao.xxx), 1);
        s.ao *= ao;
    }
    if (irradiance[1].w > .5 && irradiance[3].w > .5)
    {
        uint header = uint(irradiance[3].w) - 1;
        float reflectionMix = 0;
        float3 reflection = 0;
        float rotation = hybridRotation(uint2(input.position.xy));
        bool reflections = instances[header].world._m01 > 0;
        bool traceCoat = reflections && instances[header].world._m02 > .5;
        if (reflections)
        {
#if defined(NOVA_HALF_REFLECTIONS)
            if (!upsampleHybridReflection(pixel.xy, pos, s, traceCoat, reflection))
#endif
            {
                float unusedDistance;
                bool unusedSkip;
                reflection = hybridReflection(s, rotation, header, traceCoat, false, unusedDistance, unusedSkip);
            }
            if (instances[header].world._m30 > 2.5)
                return float4(displayRadiance(reflection), 1);
            reflectionMix = reflectionOverride >= 0 ? reflectionOverride : instances[header].world._m21;
        }
        bool diffuseBounce = instances[header].world._m31 > 0 && emissiveFactor.w > .5;
#if defined(NOVA_HYBRID_PROBES)
        bool probes = probeDimensionsFirst.x > 0 && emissiveFactor.w > .5;
#else
        bool probes = false;
#endif
        if (debug == 0 && (reflections || diffuseBounce || probes))
        {
            float3 viewDirection = cameraUp.w < 0 ? -cameraForward.xyz : normalize(eyeExposure.xyz - s.position);
            float3 probeDiffuse = 0;
            float skyCoverage = 0;
#if defined(NOVA_HYBRID_PROBES)
            if (probes && s.metallic < 1)
            {
                float coverage;
                bool probeSky = cameraForward.w > .5 && instances[header].world._m22 > .5;
                float3 probeLight = hybridSampleProbes(s.position, s.normal, probeSky, coverage);
                probeDiffuse = probeLight * s.base * (1 - s.metallic)
                               * clearcoatTransmission(s, viewDirection);
                if (probeSky)
                {
                    skyCoverage = coverage;
                    // The probe irradiance already includes sky visibility. Keep direct light and specular IBL.
                    probeDiffuse = probeDiffuseRadiance(s, viewDirection, probeLight);
                }
            }
#endif
            float3 color = surfaceRadiance(s, reflection, reflectionMix, viewDirection, traceCoat, false, skyCoverage)
                           + probeDiffuse;
            if (!probes && diffuseBounce && s.metallic < 1)
                color += hybridDiffuseBounce(s, header, rotation) * clearcoatTransmission(s, viewDirection);
            return float4(applyFog(displayRadiance(color), s.position), 1);
        }
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
    float4                position : SV_Position;
    float4                current : TEXCOORD0;
    float4                previous : TEXCOORD1;
    float2                uv : TEXCOORD2;
    nointerpolation uint  materialIndex : TEXCOORD3;
    nointerpolation float winding : TEXCOORD4;
    float                 previousDepth : TEXCOORD5;
    nointerpolation float identity : TEXCOORD6;
    nointerpolation float transmission : TEXCOORD7;
    float vertexAlpha : TEXCOORD8;
};
MotionOut MotionVSMain(uint id : SV_VertexID, uint instanceId : SV_InstanceID)
{
#ifdef NOVA_D3D12
    id += drawOffsets.x;
    instanceId += drawOffsets.y;
#endif
    bool   scene         = lightDirection.w > .5 && lightDirection.w < 1.5;
    float  palette       = scene ? instances[instanceId].normalMatrix._m03 : 1;
    uint   paletteOffset = uint(max(0, palette - 1));
    Vertex current = skinVertex(id, paletteOffset, palette > .5), old = vertices[id];
    if (skyParams.w > .5 && palette > .5)
    {
        SkinInput input = skinInputs[id];
        if (input.normal.w > 0)
        {
            uint first = uint(input.normal.w) - 1;
            uint count = skinInputs[first].joints.z;
            for (uint i = 0; i < count; ++i)
            {
                SkinInput delta = skinInputs[first + i];
                JointMatrix packed = previousJoints[paletteOffset + delta.joints.x];
                float4x4 weights = float4x4(packed.c0, packed.c1, packed.c2, packed.c3);
                uint component = delta.joints.y;
                input.position.xyz += weights[component / 4][component % 4] * delta.position.xyz;
            }
        }
        float4 position = 0;
        [unroll] for (uint j = 0; j < 4; j++)
        {
            JointMatrix m      = previousJoints[paletteOffset + input.joints[j]];
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
    o.current       = mul(viewProjection, current.position);
    o.previous      = mul(previousViewProjection, old.position);
    o.position      = o.current;
    o.previousDepth = dot(old.position.xyz - previousSceneRest[0].xyz, previousSceneRest[8].xyz);
    o.identity      = float(instanceId + 1);
    o.transmission  = current.normal.w;
    o.vertexAlpha   = current.color.a;
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
        uv             = wrapBaseUV(uv, m.emissive.w) * m.uv.xy + m.uv.zw;
        factor         = m.baseColor;
        info           = m.emissive.w;
    }
    float opacity = baseTexture.Sample(textureSampler, uv).a * factor.a * input.vertexAlpha;
    uint  flags = uint(info), mode = flags & 3;
    bool  front = input.winding < 0 ? !face : face;
    if (((flags & 4) == 0 && !front) || mode == 2 || (mode == 1 && opacity < frac(info) * 4)
        || (emissiveFactor.w > .5 && input.transmission > 0))
        discard;
    float2 current = input.current.xy / input.current.w * float2(.5, -.5) + .5,
           old     = input.previous.xy / input.previous.w * float2(.5, -.5) + .5;
    if (irradiance[1].w > .5)
        return float4(old - current, log2(max(1e-20, input.previousDepth)), input.identity);
    return float4(old - current, input.current.w / 16, .125);
}
float4 SkyMotionPSMain(SkyOut input)
  : SV_Target
{
    float4 previous = mul(previousViewProjection, float4(input.direction, 0));
    float2 uv       = previous.xy / max(previous.w, .00001) * float2(.5, -.5) + .5;
    return float4(uv - input.uv, irradiance[1].w > .5 ? 0 : 1, irradiance[1].w > .5 ? 0 : 1);
}
